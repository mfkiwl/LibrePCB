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
#include "primitivecirclegraphicsitem.h"

#include <librepcb/core/utils/toolbox.h>

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

PrimitiveCircleGraphicsItem::PrimitiveCircleGraphicsItem(
    QGraphicsItem* parent) noexcept
  : QGraphicsItem(parent),
    mLineLayer(nullptr),
    mFillLayer(nullptr),
    mShapeMode(ShapeMode::StrokeAndAreaByLayer),
    mOnLayerEditedSlot(*this, &PrimitiveCircleGraphicsItem::layerEdited) {
  setFlag(QGraphicsItem::ItemIsSelectable, true);

  mPen.setWidthF(0);
  mPenHighlighted.setWidthF(0);
  updateColors();
  updateBoundingRectAndShape();
  updateVisibility();
}

PrimitiveCircleGraphicsItem::~PrimitiveCircleGraphicsItem() noexcept {
}

/*******************************************************************************
 *  Setters
 ******************************************************************************/

void PrimitiveCircleGraphicsItem::setPosition(const Point& pos) noexcept {
  QGraphicsItem::setPos(pos.toPxQPointF());
}

void PrimitiveCircleGraphicsItem::setDiameter(
    const UnsignedLength& dia) noexcept {
  mCircleRect = Toolbox::boundingRectFromRadius(dia->toPx() / 2);
  updateBoundingRectAndShape();
}

void PrimitiveCircleGraphicsItem::setLineWidth(
    const UnsignedLength& width) noexcept {
  mPen.setWidthF(width->toPx());
  mPenHighlighted.setWidthF(width->toPx());
  updateBoundingRectAndShape();
}

void PrimitiveCircleGraphicsItem::setLineLayer(
    const std::shared_ptr<const GraphicsLayer>& layer) noexcept {
  if (mLineLayer) {
    mLineLayer->onEdited.detach(mOnLayerEditedSlot);
  }
  mLineLayer = layer;
  if (mLineLayer) {
    mLineLayer->onEdited.attach(mOnLayerEditedSlot);
  }
  updateColors();
  updateVisibility();
  updateBoundingRectAndShape();  // grab area may have changed
}

void PrimitiveCircleGraphicsItem::setFillLayer(
    const std::shared_ptr<const GraphicsLayer>& layer) noexcept {
  if (mFillLayer) {
    mFillLayer->onEdited.detach(mOnLayerEditedSlot);
  }
  mFillLayer = layer;
  if (mFillLayer) {
    mFillLayer->onEdited.attach(mOnLayerEditedSlot);
  }
  updateColors();
  updateVisibility();
  updateBoundingRectAndShape();  // grab area may have changed
}

void PrimitiveCircleGraphicsItem::setShapeMode(ShapeMode mode) noexcept {
  mShapeMode = mode;
  updateBoundingRectAndShape();
}

/*******************************************************************************
 *  Inherited from QGraphicsItem
 ******************************************************************************/

QPainterPath PrimitiveCircleGraphicsItem::shape() const noexcept {
  return ((mLineLayer && mLineLayer->isVisible()) ||
          (mFillLayer && mFillLayer->isVisible()))
      ? mShape
      : QPainterPath();
}

void PrimitiveCircleGraphicsItem::paint(QPainter* painter,
                                        const QStyleOptionGraphicsItem* option,
                                        QWidget* widget) noexcept {
  Q_UNUSED(widget);

  const bool isSelected = option->state.testFlag(QStyle::State_Selected);

  painter->setPen(isSelected ? mPenHighlighted : mPen);
  painter->setBrush(isSelected ? mBrushHighlighted : mBrush);
  painter->drawEllipse(mCircleRect);
}

/*******************************************************************************
 *  Private Methods
 ******************************************************************************/

void PrimitiveCircleGraphicsItem::layerEdited(
    const GraphicsLayer& layer, GraphicsLayer::Event event) noexcept {
  Q_UNUSED(layer);
  switch (event) {
    case GraphicsLayer::Event::ColorChanged:
    case GraphicsLayer::Event::HighlightColorChanged:
    case GraphicsLayer::Event::VisibleChanged:
    case GraphicsLayer::Event::EnabledChanged:
      updateColors();
      updateVisibility();
      break;
    default:
      qWarning() << "Unhandled switch-case in "
                    "PrimitiveCircleGraphicsItem::layerEdited():"
                 << static_cast<int>(event);
      break;
  }
}

void PrimitiveCircleGraphicsItem::updateColors() noexcept {
  if (mLineLayer && mLineLayer->isVisible()) {
    mPen.setStyle(Qt::SolidLine);
    mPenHighlighted.setStyle(Qt::SolidLine);
    mPen.setColor(mLineLayer->getColor(false));
    mPenHighlighted.setColor(mLineLayer->getColor(true));
  } else {
    mPen.setStyle(Qt::NoPen);
    mPenHighlighted.setStyle(Qt::NoPen);
  }

  if (mFillLayer && mFillLayer->isVisible()) {
    mBrush.setStyle(Qt::SolidPattern);
    mBrushHighlighted.setStyle(Qt::SolidPattern);
    mBrush.setColor(mFillLayer->getColor(false));
    mBrushHighlighted.setColor(mFillLayer->getColor(true));
  } else {
    mBrush.setStyle(Qt::NoBrush);
    mBrushHighlighted.setStyle(Qt::NoBrush);
  }
  update();
}

void PrimitiveCircleGraphicsItem::updateBoundingRectAndShape() noexcept {
  prepareGeometryChange();
  QPainterPath p;
  p.addEllipse(mCircleRect);
  if (mShapeMode == ShapeMode::FilledOutline) {
    mShape = p;
  } else {
    mShape = Toolbox::shapeFromPath(p, mPen, mBrush);
  }
  mBoundingRect = mShape.controlPointRect();
  update();
}

void PrimitiveCircleGraphicsItem::updateVisibility() noexcept {
  setVisible((mPen.style() != Qt::NoPen) || (mBrush.style() != Qt::NoBrush));
}

/*******************************************************************************
 *  End of File
 ******************************************************************************/

}  // namespace editor
}  // namespace librepcb
