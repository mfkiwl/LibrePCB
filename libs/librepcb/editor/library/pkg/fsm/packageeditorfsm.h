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

#ifndef LIBREPCB_EDITOR_PACKAGEEDITORFSM_H
#define LIBREPCB_EDITOR_PACKAGEEDITORFSM_H

/*******************************************************************************
 *  Includes
 ******************************************************************************/
#include "../../editorwidgetbase.h"

#include <librepcb/core/library/pkg/footprintpad.h>

#include <QtCore>

#include <memory>

/*******************************************************************************
 *  Namespace / Forward Declarations
 ******************************************************************************/
namespace librepcb {

class Footprint;
class Package;

namespace editor {

class FootprintGraphicsItem;
class GraphicsScene;
class GraphicsView;
class PackageEditorState;
class PackageEditorWidget;
class PrimitiveTextGraphicsItem;
class UndoStack;
struct GraphicsSceneKeyEvent;
struct GraphicsSceneMouseEvent;

/*******************************************************************************
 *  Class PackageEditorFsm
 ******************************************************************************/

/**
 * @brief The PackageEditorFsm class is the finit state machine (FSM) of the
 * package editor
 */
class PackageEditorFsm final : public QObject {
  Q_OBJECT

private:  // Types
  enum class State {
    IDLE,
    SELECT,
    ADD_THT_PADS,
    ADD_SMT_PADS_STANDARD,
    ADD_SMT_PADS_THERMAL,
    ADD_SMT_PADS_BGA,
    ADD_SMT_PADS_EDGE_CONNECTOR,
    ADD_SMT_PADS_TEST,
    ADD_SMT_PADS_LOCAL_FIDUCIAL,
    ADD_SMT_PADS_GLOBAL_FIDUCIAL,
    ADD_NAMES,
    ADD_VALUES,
    DRAW_LINE,
    DRAW_ARC,
    DRAW_RECT,
    DRAW_POLYGON,
    DRAW_CIRCLE,
    DRAW_TEXT,
    DRAW_ZONE,
    ADD_HOLES,
    MEASURE,
    RENUMBER_PADS,
  };

public:  // Types
  struct Context {
    EditorWidgetBase::Context& editorContext;
    PackageEditorWidget& editorWidget;
    UndoStack& undoStack;
    GraphicsScene& graphicsScene;
    GraphicsView& graphicsView;
    LengthUnit& lengthUnit;
    Package& package;
    std::shared_ptr<Footprint> currentFootprint;
    std::shared_ptr<FootprintGraphicsItem> currentGraphicsItem;
    ToolBarProxy& commandToolBar;
  };

public:
  // Constructors / Destructor
  PackageEditorFsm() = delete;
  PackageEditorFsm(const PackageEditorFsm& other) = delete;
  explicit PackageEditorFsm(const Context& context) noexcept;
  virtual ~PackageEditorFsm() noexcept;

  // Getters
  EditorWidgetBase::Tool getCurrentTool() const noexcept;
  std::shared_ptr<Footprint> getCurrentFootprint() const noexcept;
  const QSet<EditorWidgetBase::Feature>& getAvailableFeatures() const noexcept {
    return mAvailableFeatures;
  }

  // General Methods
  void updateAvailableFeatures() noexcept;

  // Event Handlers
  bool processChangeCurrentFootprint(
      const std::shared_ptr<Footprint>& fpt) noexcept;
  bool processKeyPressed(const GraphicsSceneKeyEvent& e) noexcept;
  bool processKeyReleased(const GraphicsSceneKeyEvent& e) noexcept;
  bool processGraphicsSceneMouseMoved(
      const GraphicsSceneMouseEvent& e) noexcept;
  bool processGraphicsSceneLeftMouseButtonPressed(
      const GraphicsSceneMouseEvent& e) noexcept;
  bool processGraphicsSceneLeftMouseButtonReleased(
      const GraphicsSceneMouseEvent& e) noexcept;
  bool processGraphicsSceneLeftMouseButtonDoubleClicked(
      const GraphicsSceneMouseEvent& e) noexcept;
  bool processGraphicsSceneRightMouseButtonReleased(
      const GraphicsSceneMouseEvent& e) noexcept;
  bool processSelectAll() noexcept;
  bool processCut() noexcept;
  bool processCopy() noexcept;
  bool processPaste() noexcept;
  bool processMove(const Point& delta) noexcept;
  bool processRotate(const Angle& rotation) noexcept;
  bool processMirror(Qt::Orientation orientation) noexcept;
  bool processFlip(Qt::Orientation orientation) noexcept;
  bool processMoveAlign() noexcept;
  bool processSnapToGrid() noexcept;
  bool processRemove() noexcept;
  bool processEditProperties() noexcept;
  bool processGenerateOutline() noexcept;
  bool processGenerateCourtyard() noexcept;
  bool processAbortCommand() noexcept;
  bool processStartSelecting() noexcept;
  bool processStartAddingFootprintThtPads() noexcept;
  bool processStartAddingFootprintSmtPads(
      FootprintPad::Function function) noexcept;
  bool processStartAddingNames() noexcept;
  bool processStartAddingValues() noexcept;
  bool processStartDrawLines() noexcept;
  bool processStartDrawArcs() noexcept;
  bool processStartDrawRects() noexcept;
  bool processStartDrawPolygons() noexcept;
  bool processStartDrawCircles() noexcept;
  bool processStartDrawTexts() noexcept;
  bool processStartDrawZones() noexcept;
  bool processStartAddingHoles() noexcept;
  bool processStartDxfImport() noexcept;
  bool processStartMeasure() noexcept;
  bool processStartReNumberPads() noexcept;

  // Operator Overloadings
  PackageEditorFsm& operator=(const PackageEditorFsm& rhs) = delete;

signals:
  void toolChanged(EditorWidgetBase::Tool newTool);
  void availableFeaturesChanged();
  void statusBarMessageChanged(const QString& message, int timeoutMs = -1);

private:  // Methods
  PackageEditorState* getCurrentState() const noexcept;
  bool setNextState(State state) noexcept;
  bool leaveCurrentState() noexcept;
  bool enterNextState(State state) noexcept;
  bool switchToPreviousState() noexcept;

private:  // Data
  Context mContext;
  QMap<State, PackageEditorState*> mStates;
  State mCurrentState;
  State mPreviousState;
  QScopedPointer<PrimitiveTextGraphicsItem> mSelectFootprintGraphicsItem;
  QSet<EditorWidgetBase::Feature> mAvailableFeatures;
};

/*******************************************************************************
 *  End of File
 ******************************************************************************/

}  // namespace editor
}  // namespace librepcb

#endif
