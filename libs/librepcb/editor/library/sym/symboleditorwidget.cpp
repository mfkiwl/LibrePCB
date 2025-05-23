/*
 * LibrePCB - Professional EDA for everyone!
 * Copyright (C) 2013 LibrePCB Developers, see AUTHORS.md for contributors.
 * https://librepcb.org/
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*******************************************************************************
 *  Includes
 ******************************************************************************/
#include "symboleditorwidget.h"

#include "../../cmd/cmdtextedit.h"
#include "../../dialogs/gridsettingsdialog.h"
#include "../../editorcommandset.h"
#include "../../graphics/circlegraphicsitem.h"
#include "../../graphics/graphicsscene.h"
#include "../../library/cmd/cmdlibraryelementedit.h"
#include "../../utils/exclusiveactiongroup.h"
#include "../../utils/toolbarproxy.h"
#include "../../widgets/statusbar.h"
#include "../../workspace/desktopservices.h"
#include "../cmd/cmdsymbolpinedit.h"
#include "fsm/symboleditorfsm.h"
#include "symbolgraphicsitem.h"
#include "ui_symboleditorwidget.h"

#include <librepcb/core/library/cmp/cmpsigpindisplaytype.h>
#include <librepcb/core/library/librarybaseelementcheckmessages.h>
#include <librepcb/core/library/libraryelementcheckmessages.h>
#include <librepcb/core/library/sym/symbol.h>
#include <librepcb/core/library/sym/symbolcheckmessages.h>
#include <librepcb/core/library/sym/symbolpainter.h>
#include <librepcb/core/workspace/workspace.h>
#include <librepcb/core/workspace/workspacesettings.h>

#include <QtCore>
#include <QtWidgets>

/*******************************************************************************
 *  Namespace
 ******************************************************************************/
namespace librepcb {
namespace editor {

/*******************************************************************************
 *  Constructors / Destructor
 ******************************************************************************/

SymbolEditorWidget::SymbolEditorWidget(const Context& context,
                                       const FilePath& fp, QWidget* parent)
  : EditorWidgetBase(context, fp, parent),
    mUi(new Ui::SymbolEditorWidget),
    mGraphicsScene(new GraphicsScene()) {
  mUi->setupUi(this);
  mUi->lstMessages->setHandler(this);
  mUi->lstMessages->setReadOnly(mContext.readOnly);
  mUi->edtName->setReadOnly(mContext.readOnly);
  mUi->edtDescription->setReadOnly(mContext.readOnly);
  mUi->edtKeywords->setReadOnly(mContext.readOnly);
  mUi->edtAuthor->setReadOnly(mContext.readOnly);
  mUi->edtVersion->setReadOnly(mContext.readOnly);
  mUi->cbxDeprecated->setCheckable(!mContext.readOnly);
  setupErrorNotificationWidget(*mUi->errorNotificationWidget);
  setWindowIcon(QIcon(":/img/library/symbol.png"));

  // Setup graphics scene.
  const Theme& theme = mContext.workspace.getSettings().themes.getActive();
  mGraphicsScene->setBackgroundColors(
      theme.getColor(Theme::Color::sSchematicBackground).getPrimaryColor(),
      theme.getColor(Theme::Color::sSchematicBackground).getSecondaryColor());
  mGraphicsScene->setOverlayColors(
      theme.getColor(Theme::Color::sSchematicOverlays).getPrimaryColor(),
      theme.getColor(Theme::Color::sSchematicOverlays).getSecondaryColor());
  mGraphicsScene->setSelectionRectColors(
      theme.getColor(Theme::Color::sSchematicSelection).getPrimaryColor(),
      theme.getColor(Theme::Color::sSchematicSelection).getSecondaryColor());
  mGraphicsScene->setGridStyle(theme.getBoardGridStyle());

  // Setup graphics view.
  mUi->graphicsView->setSpinnerColor(
      theme.getColor(Theme::Color::sSchematicBackground).getSecondaryColor());
  mUi->graphicsView->setInfoBoxColors(
      theme.getColor(Theme::Color::sSchematicInfoBox).getPrimaryColor(),
      theme.getColor(Theme::Color::sSchematicInfoBox).getSecondaryColor());
  mUi->graphicsView->setUseOpenGl(
      mContext.workspace.getSettings().useOpenGl.get());
  mUi->graphicsView->setScene(mGraphicsScene.data());
  mUi->graphicsView->addAction(
      EditorCommandSet::instance().commandToolBarFocus.createAction(
          this, this,
          [this]() {
            mCommandToolBarProxy->startTabFocusCycle(*mUi->graphicsView);
          },
          EditorCommand::ActionFlag::WidgetShortcut));

  // Apply grid properties unit from workspace settings
  setGridProperties(PositiveLength(2540000),
                    mContext.workspace.getSettings().defaultLengthUnit.get(),
                    theme.getBoardGridStyle());

  // Insert category list editor widget.
  mCategoriesEditorWidget.reset(new CategoryListEditorWidget(
      mContext.workspace, CategoryListEditorWidget::Categories::Component,
      this));
  mCategoriesEditorWidget->setReadOnly(mContext.readOnly);
  mCategoriesEditorWidget->setRequiresMinimumOneEntry(true);
  int row;
  QFormLayout::ItemRole role;
  mUi->formLayout->getWidgetPosition(mUi->lblCategories, &row, &role);
  mUi->formLayout->setWidget(row, QFormLayout::FieldRole,
                             mCategoriesEditorWidget.data());

  // Load element.
  mSymbol = Symbol::open(std::unique_ptr<TransactionalDirectory>(
      new TransactionalDirectory(mFileSystem)));  // can throw
  updateMetadata();

  // Show "interface broken" warning when related properties are modified.
  mOriginalSymbolPinUuids = mSymbol->getPins().getUuidSet();
  setupInterfaceBrokenWarningWidget(*mUi->interfaceBrokenWarningWidget);

  // Reload metadata on undo stack state changes.
  connect(mUndoStack.data(), &UndoStack::stateModified, this,
          &SymbolEditorWidget::updateMetadata);

  // Handle changes of metadata.
  connect(mUi->edtName, &QLineEdit::editingFinished, this,
          &SymbolEditorWidget::commitMetadata);
  connect(mUi->edtDescription, &PlainTextEdit::editingFinished, this,
          &SymbolEditorWidget::commitMetadata);
  connect(mUi->edtKeywords, &QLineEdit::editingFinished, this,
          &SymbolEditorWidget::commitMetadata);
  connect(mUi->edtAuthor, &QLineEdit::editingFinished, this,
          &SymbolEditorWidget::commitMetadata);
  connect(mUi->edtVersion, &QLineEdit::editingFinished, this,
          &SymbolEditorWidget::commitMetadata);
  connect(mUi->cbxDeprecated, &QCheckBox::clicked, this,
          &SymbolEditorWidget::commitMetadata);
  connect(mCategoriesEditorWidget.data(), &CategoryListEditorWidget::edited,
          this, &SymbolEditorWidget::commitMetadata);

  // Load graphics items recursively.
  mGraphicsItem.reset(new SymbolGraphicsItem(*mSymbol, mContext.layers));
  mGraphicsScene->addItem(*mGraphicsItem);
  mUi->graphicsView->zoomAll();

  // Load finite state machine (FSM).
  SymbolEditorFsm::Context fsmContext{mContext,
                                      *this,
                                      *mUndoStack,
                                      *mGraphicsScene,
                                      *mUi->graphicsView,
                                      mLengthUnit,
                                      *mSymbol,
                                      *mGraphicsItem,
                                      *mCommandToolBarProxy};
  mFsm.reset(new SymbolEditorFsm(fsmContext));
  connect(mUndoStack.data(), &UndoStack::stateModified, mFsm.data(),
          &SymbolEditorFsm::updateAvailableFeatures);
  connect(mFsm.data(), &SymbolEditorFsm::availableFeaturesChanged, this,
          [this]() { emit availableFeaturesChanged(getAvailableFeatures()); });
  connect(mFsm.data(), &SymbolEditorFsm::statusBarMessageChanged, this,
          &SymbolEditorWidget::setStatusBarMessage);

  // Last but not least, connect the graphics scene events with the FSM.
  mUi->graphicsView->setEventHandlerObject(this);
}

SymbolEditorWidget::~SymbolEditorWidget() noexcept {
  // Clean up the state machine nicely to avoid unexpected behavior. Triggering
  // abort (Esc) two times is usually sufficient to leave any active tool, so
  // let's call it three times to be on the safe side. Unfortunately there's
  // no clean way to forcible and guaranteed leaving a tool.
  mFsm->processAbortCommand();
  mFsm->processAbortCommand();
  mFsm->processAbortCommand();
  mFsm.reset();

  // Delete all command objects in the undo stack. This mmust be done before
  // other important objects are deleted, as undo command objects can hold
  // pointers/references to them!
  mUndoStack->clear();
}

/*******************************************************************************
 *  Getters
 ******************************************************************************/

QSet<EditorWidgetBase::Feature> SymbolEditorWidget::getAvailableFeatures()
    const noexcept {
  QSet<EditorWidgetBase::Feature> features = {
      EditorWidgetBase::Feature::Close,
      EditorWidgetBase::Feature::GraphicsView,
      EditorWidgetBase::Feature::ExportGraphics,
  };
  return features + mFsm->getAvailableFeatures();
}

/*******************************************************************************
 *  Setters
 ******************************************************************************/

void SymbolEditorWidget::connectEditor(
    UndoStackActionGroup& undoStackActionGroup,
    ExclusiveActionGroup& toolsActionGroup, QToolBar& commandToolBar,
    StatusBar& statusBar) noexcept {
  EditorWidgetBase::connectEditor(undoStackActionGroup, toolsActionGroup,
                                  commandToolBar, statusBar);

  bool enabled = !mContext.readOnly;
  mToolsActionGroup->setActionEnabled(Tool::SELECT, true);
  mToolsActionGroup->setActionEnabled(Tool::ADD_PINS, enabled);
  mToolsActionGroup->setActionEnabled(Tool::ADD_NAMES, enabled);
  mToolsActionGroup->setActionEnabled(Tool::ADD_VALUES, enabled);
  mToolsActionGroup->setActionEnabled(Tool::DRAW_LINE, enabled);
  mToolsActionGroup->setActionEnabled(Tool::DRAW_RECT, enabled);
  mToolsActionGroup->setActionEnabled(Tool::DRAW_POLYGON, enabled);
  mToolsActionGroup->setActionEnabled(Tool::DRAW_CIRCLE, enabled);
  mToolsActionGroup->setActionEnabled(Tool::DRAW_ARC, enabled);
  mToolsActionGroup->setActionEnabled(Tool::DRAW_TEXT, enabled);
  mToolsActionGroup->setActionEnabled(Tool::MEASURE, true);
  mToolsActionGroup->setCurrentAction(mFsm->getCurrentTool());
  connect(mFsm.data(), &SymbolEditorFsm::toolChanged, mToolsActionGroup,
          &ExclusiveActionGroup::setCurrentAction);

  mStatusBar->setField(StatusBar::AbsolutePosition, true);
  mStatusBar->setLengthUnit(mLengthUnit);
  connect(mUi->graphicsView, &GraphicsView::cursorScenePositionChanged,
          mStatusBar, &StatusBar::setAbsoluteCursorPosition);
}

void SymbolEditorWidget::disconnectEditor() noexcept {
  disconnect(mFsm.data(), &SymbolEditorFsm::toolChanged, mToolsActionGroup,
             &ExclusiveActionGroup::setCurrentAction);

  mStatusBar->setField(StatusBar::AbsolutePosition, false);
  disconnect(mUi->graphicsView, &GraphicsView::cursorScenePositionChanged,
             mStatusBar, &StatusBar::setAbsoluteCursorPosition);

  EditorWidgetBase::disconnectEditor();
}

/*******************************************************************************
 *  Public Slots
 ******************************************************************************/

bool SymbolEditorWidget::save() noexcept {
  // Remove obsolete message approvals (bypassing the undo stack).
  mSymbol->setMessageApprovals(mSymbol->getMessageApprovals() -
                               mDisappearedApprovals);

  // Commit metadata.
  QString errorMsg = commitMetadata();
  if (!errorMsg.isEmpty()) {
    QMessageBox::critical(this, tr("Invalid metadata"), errorMsg);
    return false;
  }

  // Save element.
  try {
    mSymbol->save();  // can throw
    mFileSystem->save();  // can throw
    mOriginalSymbolPinUuids = mSymbol->getPins().getUuidSet();
    return EditorWidgetBase::save();
  } catch (const Exception& e) {
    QMessageBox::critical(this, tr("Save failed"), e.getMsg());
    return false;
  }
}

bool SymbolEditorWidget::selectAll() noexcept {
  return mFsm->processSelectAll();
}

bool SymbolEditorWidget::cut() noexcept {
  return mFsm->processCut();
}

bool SymbolEditorWidget::copy() noexcept {
  return mFsm->processCopy();
}

bool SymbolEditorWidget::paste() noexcept {
  return mFsm->processPaste();
}

bool SymbolEditorWidget::move(Qt::ArrowType direction) noexcept {
  Point delta;
  switch (direction) {
    case Qt::LeftArrow: {
      delta.setX(-mGraphicsScene->getGridInterval());
      break;
    }
    case Qt::RightArrow: {
      delta.setX(*mGraphicsScene->getGridInterval());
      break;
    }
    case Qt::UpArrow: {
      delta.setY(*mGraphicsScene->getGridInterval());
      break;
    }
    case Qt::DownArrow: {
      delta.setY(-mGraphicsScene->getGridInterval());
      break;
    }
    default: {
      qWarning() << "Unhandled switch-case in SymbolEditorWidget::move():"
                 << direction;
      break;
    }
  }
  return mFsm->processMove(delta);
}

bool SymbolEditorWidget::rotate(const Angle& rotation) noexcept {
  return mFsm->processRotate(rotation);
}

bool SymbolEditorWidget::mirror(Qt::Orientation orientation) noexcept {
  return mFsm->processMirror(orientation);
}

bool SymbolEditorWidget::snapToGrid() noexcept {
  return mFsm->processSnapToGrid();
}

bool SymbolEditorWidget::remove() noexcept {
  return mFsm->processRemove();
}

bool SymbolEditorWidget::editProperties() noexcept {
  return mFsm->processEditProperties();
}

bool SymbolEditorWidget::zoomIn() noexcept {
  mUi->graphicsView->zoomIn();
  return true;
}

bool SymbolEditorWidget::zoomOut() noexcept {
  mUi->graphicsView->zoomOut();
  return true;
}

bool SymbolEditorWidget::zoomAll() noexcept {
  mUi->graphicsView->zoomAll();
  return true;
}

bool SymbolEditorWidget::abortCommand() noexcept {
  return mFsm->processAbortCommand();
}

bool SymbolEditorWidget::importDxf() noexcept {
  return mFsm->processStartDxfImport();
}

bool SymbolEditorWidget::editGridProperties() noexcept {
  GridSettingsDialog dialog(mGraphicsScene->getGridInterval(), mLengthUnit,
                            mGraphicsScene->getGridStyle(), this);
  connect(&dialog, &GridSettingsDialog::gridPropertiesChanged, this,
          &SymbolEditorWidget::setGridProperties);
  dialog.exec();
  return true;
}

bool SymbolEditorWidget::increaseGridInterval() noexcept {
  const Length interval = mGraphicsScene->getGridInterval() * 2;
  setGridProperties(PositiveLength(interval), mLengthUnit,
                    mGraphicsScene->getGridStyle());
  return true;
}

bool SymbolEditorWidget::decreaseGridInterval() noexcept {
  const Length interval = *mGraphicsScene->getGridInterval();
  if ((interval % 2) == 0) {
    setGridProperties(PositiveLength(interval / 2), mLengthUnit,
                      mGraphicsScene->getGridStyle());
  }
  return true;
}

/*******************************************************************************
 *  Private Methods
 ******************************************************************************/

void SymbolEditorWidget::updateMetadata() noexcept {
  setWindowTitle(*mSymbol->getNames().getDefaultValue());
  mUi->edtName->setText(*mSymbol->getNames().getDefaultValue());
  mUi->edtDescription->setPlainText(
      mSymbol->getDescriptions().getDefaultValue());
  mUi->edtKeywords->setText(mSymbol->getKeywords().getDefaultValue());
  mUi->edtAuthor->setText(mSymbol->getAuthor());
  mUi->edtVersion->setText(mSymbol->getVersion().toStr());
  mUi->cbxDeprecated->setChecked(mSymbol->isDeprecated());
  mUi->lstMessages->setApprovals(mSymbol->getMessageApprovals());
  mCategoriesEditorWidget->setUuids(mSymbol->getCategories());
}

QString SymbolEditorWidget::commitMetadata() noexcept {
  try {
    std::unique_ptr<CmdLibraryElementEdit> cmd(
        new CmdLibraryElementEdit(*mSymbol, tr("Edit symbol metadata")));
    try {
      // throws on invalid name
      cmd->setName("", ElementName(mUi->edtName->text().trimmed()));
    } catch (const Exception& e) {
    }
    cmd->setDescription("", mUi->edtDescription->toPlainText().trimmed());
    cmd->setKeywords("", mUi->edtKeywords->text().trimmed());
    try {
      // throws on invalid version
      cmd->setVersion(Version::fromString(mUi->edtVersion->text().trimmed()));
    } catch (const Exception& e) {
    }
    cmd->setAuthor(mUi->edtAuthor->text().trimmed());
    cmd->setDeprecated(mUi->cbxDeprecated->isChecked());
    cmd->setCategories(mCategoriesEditorWidget->getUuids());

    // Commit all changes.
    mUndoStack->execCmd(cmd.release());  // can throw

    // Reload metadata into widgets to discard invalid input.
    updateMetadata();
  } catch (const Exception& e) {
    return e.getMsg();
  }
  return QString();
}

bool SymbolEditorWidget::graphicsSceneKeyPressed(
    const GraphicsSceneKeyEvent& e) noexcept {
  return mFsm->processKeyPressed(e);
}

bool SymbolEditorWidget::graphicsSceneKeyReleased(
    const GraphicsSceneKeyEvent& e) noexcept {
  return mFsm->processKeyReleased(e);
}

bool SymbolEditorWidget::graphicsSceneMouseMoved(
    const GraphicsSceneMouseEvent& e) noexcept {
  return mFsm->processGraphicsSceneMouseMoved(e);
}

bool SymbolEditorWidget::graphicsSceneLeftMouseButtonPressed(
    const GraphicsSceneMouseEvent& e) noexcept {
  return mFsm->processGraphicsSceneLeftMouseButtonPressed(e);
}

bool SymbolEditorWidget::graphicsSceneLeftMouseButtonReleased(
    const GraphicsSceneMouseEvent& e) noexcept {
  return mFsm->processGraphicsSceneLeftMouseButtonReleased(e);
}

bool SymbolEditorWidget::graphicsSceneLeftMouseButtonDoubleClicked(
    const GraphicsSceneMouseEvent& e) noexcept {
  return mFsm->processGraphicsSceneLeftMouseButtonDoubleClicked(e);
}

bool SymbolEditorWidget::graphicsSceneRightMouseButtonReleased(
    const GraphicsSceneMouseEvent& e) noexcept {
  return mFsm->processGraphicsSceneRightMouseButtonReleased(e);
}

bool SymbolEditorWidget::toolChangeRequested(Tool newTool,
                                             const QVariant& mode) noexcept {
  Q_UNUSED(mode);
  switch (newTool) {
    case Tool::SELECT:
      return mFsm->processStartSelecting();
    case Tool::ADD_PINS:
      return mFsm->processStartAddingSymbolPins();
    case Tool::ADD_NAMES:
      return mFsm->processStartAddingNames();
    case Tool::ADD_VALUES:
      return mFsm->processStartAddingValues();
    case Tool::DRAW_LINE:
      return mFsm->processStartDrawLines();
    case Tool::DRAW_RECT:
      return mFsm->processStartDrawRects();
    case Tool::DRAW_POLYGON:
      return mFsm->processStartDrawPolygons();
    case Tool::DRAW_CIRCLE:
      return mFsm->processStartDrawCircles();
    case Tool::DRAW_ARC:
      return mFsm->processStartDrawArcs();
    case Tool::DRAW_TEXT:
      return mFsm->processStartDrawTexts();
    case Tool::MEASURE:
      return mFsm->processStartMeasure();
    default:
      return false;
  }
}

bool SymbolEditorWidget::isInterfaceBroken() const noexcept {
  return mSymbol->getPins().getUuidSet() != mOriginalSymbolPinUuids;
}

bool SymbolEditorWidget::runChecks(RuleCheckMessageList& msgs) const {
  if ((mFsm->getCurrentTool() != NONE) && (mFsm->getCurrentTool() != SELECT)) {
    // Do not run checks if a tool is active because it could lead to annoying,
    // flickering messages. For example when placing pins, they always overlap
    // right after placing them, so we have to wait until the user has moved the
    // cursor to place the pin at a different position.
    return false;
  }
  msgs = mSymbol->runChecks();  // can throw
  mUi->lstMessages->setMessages(msgs);
  return true;
}

template <>
void SymbolEditorWidget::fixMsg(const MsgNameNotTitleCase& msg) {
  mUi->edtName->setText(*msg.getFixedName());
  commitMetadata();
}

template <>
void SymbolEditorWidget::fixMsg(const MsgMissingAuthor& msg) {
  Q_UNUSED(msg);
  mUi->edtAuthor->setText(getWorkspaceSettingsUserName());
  commitMetadata();
}

template <>
void SymbolEditorWidget::fixMsg(const MsgMissingCategories& msg) {
  Q_UNUSED(msg);
  mCategoriesEditorWidget->openAddCategoryDialog();
}

template <>
void SymbolEditorWidget::fixMsg(const MsgMissingSymbolName& msg) {
  Q_UNUSED(msg);
  mFsm->processStartAddingNames();
}

template <>
void SymbolEditorWidget::fixMsg(const MsgMissingSymbolValue& msg) {
  Q_UNUSED(msg);
  mFsm->processStartAddingValues();
}

template <>
void SymbolEditorWidget::fixMsg(const MsgWrongSymbolTextLayer& msg) {
  std::shared_ptr<Text> text = mSymbol->getTexts().get(msg.getText().get());
  std::unique_ptr<CmdTextEdit> cmd(new CmdTextEdit(*text));
  cmd->setLayer(msg.getExpectedLayer(), false);
  mUndoStack->execCmd(cmd.release());
}

template <>
void SymbolEditorWidget::fixMsg(const MsgSymbolPinNotOnGrid& msg) {
  std::shared_ptr<SymbolPin> pin = mSymbol->getPins().get(msg.getPin().get());
  Point newPos = pin->getPosition().mappedToGrid(msg.getGridInterval());
  std::unique_ptr<CmdSymbolPinEdit> cmd(new CmdSymbolPinEdit(pin));
  cmd->setPosition(newPos, false);
  mUndoStack->execCmd(cmd.release());
}

template <>
void SymbolEditorWidget::fixMsg(
    const MsgNonFunctionalSymbolPinInversionSign& msg) {
  std::shared_ptr<SymbolPin> pin = mSymbol->getPins().get(msg.getPin().get());
  std::unique_ptr<CmdSymbolPinEdit> cmd(new CmdSymbolPinEdit(pin));
  cmd->setName(CircuitIdentifier("!" % pin->getName()->mid(1)), false);
  mUndoStack->execCmd(cmd.release());
}

template <>
void SymbolEditorWidget::fixMsg(const MsgSymbolOriginNotInCenter& msg) {
  mFsm->processAbortCommand();
  mFsm->processAbortCommand();
  mFsm->processSelectAll();
  mFsm->processMove(
      -msg.getCenter().mappedToGrid(mGraphicsScene->getGridInterval()));
  mFsm->processAbortCommand();  // Clear selection.
}

template <typename MessageType>
bool SymbolEditorWidget::fixMsgHelper(
    std::shared_ptr<const RuleCheckMessage> msg, bool applyFix) {
  if (msg) {
    if (auto m = msg->as<MessageType>()) {
      if (applyFix) fixMsg(*m);  // can throw
      return true;
    }
  }
  return false;
}

bool SymbolEditorWidget::processRuleCheckMessage(
    std::shared_ptr<const RuleCheckMessage> msg, bool applyFix) {
  if (fixMsgHelper<MsgNameNotTitleCase>(msg, applyFix)) return true;
  if (fixMsgHelper<MsgMissingAuthor>(msg, applyFix)) return true;
  if (fixMsgHelper<MsgMissingCategories>(msg, applyFix)) return true;
  if (fixMsgHelper<MsgMissingSymbolName>(msg, applyFix)) return true;
  if (fixMsgHelper<MsgMissingSymbolValue>(msg, applyFix)) return true;
  if (fixMsgHelper<MsgWrongSymbolTextLayer>(msg, applyFix)) return true;
  if (fixMsgHelper<MsgSymbolPinNotOnGrid>(msg, applyFix)) return true;
  if (fixMsgHelper<MsgNonFunctionalSymbolPinInversionSign>(msg, applyFix))
    return true;
  if (fixMsgHelper<MsgSymbolOriginNotInCenter>(msg, applyFix)) return true;
  return false;
}

void SymbolEditorWidget::ruleCheckApproveRequested(
    std::shared_ptr<const RuleCheckMessage> msg, bool approve) noexcept {
  setMessageApproved(*mSymbol, msg, approve);
  updateMetadata();
}

bool SymbolEditorWidget::execGraphicsExportDialog(
    GraphicsExportDialog::Output output, const QString& settingsKey) noexcept {
  try {
    // Determine default file path.
    QString symbolName =
        FilePath::cleanFileName(*mSymbol->getNames().getDefaultValue(),
                                FilePath::ReplaceSpaces | FilePath::KeepCase);
    FilePath defaultFilePath(QDir::homePath() % "/" % symbolName % "_Symbol");

    // Copy symbol items to allow processing them in worker threads.
    QList<std::shared_ptr<GraphicsPagePainter>> pages = {
        std::make_shared<SymbolPainter>(*mSymbol),
    };

    // Show dialog, which will do all the work.
    GraphicsExportDialog dialog(
        GraphicsExportDialog::Mode::Schematic, output, pages, 0,
        *mSymbol->getNames().getDefaultValue(), 0, defaultFilePath,
        mContext.workspace.getSettings().defaultLengthUnit.get(),
        mContext.workspace.getSettings().themes.getActive(),
        "symbol_editor/" % settingsKey, this);
    connect(&dialog, &GraphicsExportDialog::requestOpenFile, this,
            [this](const FilePath& fp) {
              DesktopServices ds(mContext.workspace.getSettings());
              ds.openLocalPath(fp);
            });
    dialog.exec();
  } catch (const Exception& e) {
    QMessageBox::warning(this, tr("Error"), e.getMsg());
  }
  return true;
}

void SymbolEditorWidget::setGridProperties(const PositiveLength& interval,
                                           const LengthUnit& unit,
                                           Theme::GridStyle style) noexcept {
  mGraphicsScene->setGridInterval(interval);
  mGraphicsScene->setGridStyle(style);
  mLengthUnit = unit;
  if (mStatusBar) {
    mStatusBar->setLengthUnit(unit);
  }
  if (mFsm) {
    mFsm->updateAvailableFeatures();  // Re-calculate "snap to grid" feature!
  }
}

/*******************************************************************************
 *  End of File
 ******************************************************************************/

}  // namespace editor
}  // namespace librepcb
