/*
 *  kis_tool_brush.cc - part of Krayon
 *
 *  Copyright (c) 1999 Matthias Elter <me@kde.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <qbitmap.h>
#include <qcolor.h>

#include <kaction.h>
#include <kapplication.h>
#include <kdebug.h>

#include "kis_brush.h"
#include "kis_canvas.h"
#include "kis_cursor.h"
#include "kis_doc.h"
#include "kis_dlg_toolopts.h"
#include "kis_tool_brush.h"
#include "kis_undo.h"
#include "kis_util.h"
#include "kis_view.h"
#include "kis_vec.h"

class BrushToolCmd : public KisCommand {
public:
	BrushToolCmd(KisDoc *doc);
	virtual ~BrushToolCmd();

	virtual void execute();
	virtual void unexecute();
};

BrushToolCmd::BrushToolCmd(KisDoc *doc) : KisCommand(doc)
{
}

BrushToolCmd::~BrushToolCmd()
{
}

void BrushToolCmd::execute()
{
	kdDebug() << "BrushToolCmd::execute\n";
}

void BrushToolCmd::unexecute()
{
	kdDebug() << "BrushToolCmd::unexecute\n";
}

BrushTool::BrushTool(KisDoc *doc, KisBrush *brush) : KisTool(doc)
{
	m_doc = doc;
	m_dragging = false;
	m_dragdist = 0;

	// initialize brush tool settings
	m_usePattern = false;
	m_useGradient = false;
	m_opacity = 255;

	setBrush(brush);
}

BrushTool::~BrushTool() 
{
}

void BrushTool::setBrush(KisBrush *brush)
{
	m_brush = brush;
	m_brushWidth = m_brush -> pixmap().width();
	m_brushHeight = m_brush -> pixmap().height();
	m_hotSpot  = m_brush -> hotSpot();
	m_hotSpotX = m_brush -> hotSpot().x();
	m_hotSpotY = m_brush -> hotSpot().y();
	m_brushSize = QSize(m_brushWidth, m_brushHeight);

	// make custom cursor from brush pixmap
	// if brush pixmap is of reasonable size
	if (m_brushWidth < 33 && m_brushHeight < 33 && m_brushWidth > 9 && m_brushHeight > 9) {
		QBitmap mask(m_brushWidth, m_brushHeight);
		QPixmap pix(m_brush -> pixmap());

		mask = pix.createHeuristicMask();
		pix.setMask(mask);
		m_view -> kisCanvas() -> setCursor(QCursor(pix));
		m_cursor = QCursor(pix);
	}
	// use default brush cursor
	else {
		m_view -> kisCanvas() -> setCursor(KisCursor::brushCursor());
		m_cursor = KisCursor::brushCursor();
	}
}

void BrushTool::mousePress(QMouseEvent *e)
{
	KisImage *img;
	KisLayer *lay;

	if (!(img = m_doc -> current()))
		return;

	if (!(lay = img -> getCurrentLayer()))
		return;

	if (!lay -> visible())
		return;

	if (e -> button() != QMouseEvent::LeftButton)
		return;

	m_red = m_view -> fgColor().R();
	m_green = m_view -> fgColor().G();
	m_blue = m_view -> fgColor().B();
	m_alpha = img -> colorMode() == cm_RGBA;
	m_spacing = m_brush -> spacing();

	if (m_spacing <= 0) 
		m_spacing = 3;

	m_dragging = true;

	QPoint pos = e -> pos();
	pos = zoomed(pos);
	m_dragStart = pos;
	m_dragdist = 0;

	if (paintMonochrome(pos)) {
		QRect rc(pos - m_hotSpot, m_brushSize);

		img -> markDirty(rc);
	}
}

bool BrushTool::paintCanvas(const QPoint& /* pos */)
{
	return true;
}

bool BrushTool::paintMonochrome(const QPoint& pos)
{
	KisImage *img = m_doc -> current();
	KisLayer *lay = img -> getCurrentLayer();
	int startx = pos.x() - m_hotSpotX;
	int starty = pos.y() - m_hotSpotY;
	QRect clipRect(startx, starty, m_brushWidth, m_brushHeight);

	if (!clipRect.intersects(lay -> imageExtents()))
		return false;

	clipRect = clipRect.intersect(lay -> imageExtents());

	int sx = clipRect.left() - startx;
	int sy = clipRect.top() - starty;
	int ex = clipRect.right() - startx;
	int ey = clipRect.bottom() - starty;
	int invopacity = 255 - m_opacity;

	uchar *sl;
	uchar bv, invbv;
	uchar r, g, b, a;
	int   v;
	uint rgb;

	for (int y = sy; y <= ey; y++) {
		sl = m_brush -> scanline(y);

		for (int x = sx; x <= ex; x++) {
			lay -> pixel(startx + x, starty + y, &rgb);
			r = qRed(rgb);
			g = qGreen(rgb);
			b = qBlue(rgb);
			bv = *(sl + x);

			if (bv == 0)
				continue;

			invbv = 255 - bv;
			b = (m_blue * m_opacity + r * invopacity) / 255;
			g = (m_green * m_opacity + g * invopacity) / 255;
			r = (m_red * m_opacity + r * invopacity) / 255;
			rgb = (qRgb(m_red, m_green, m_red) * m_opacity + rgb * invopacity) / 255;

			if (m_alpha) {
				a = qAlpha(rgb);
				v = a + bv;

				if (v < 0) 
					v = 0;

				if (v > 255) 
					v = 255;

				a = (uchar) v;
				rgb = qRgba(r, g, b, a);
			}
			else 
				rgb = qRgb(r, g, b);

			lay -> setPixel(startx + x, starty + y, rgb);
		}
	}

	return true;
}

void BrushTool::mouseMove(QMouseEvent *e)
{
	if (!m_dragging)
		return;

	KisImage *img = m_doc -> current();

	if (!img) 
		return;

	QPoint pos = zoomed(e -> pos());
	int mouseX = e -> x();
	int mouseY = e -> y();
	KisVector end(mouseX, mouseY);
	KisVector start(m_dragStart.x(), m_dragStart.y());
	KisVector dragVec = end - start;
	float saved_dist = m_dragdist;
	float new_dist = dragVec.length();
	float dist = saved_dist + new_dist;

	if (static_cast<int>(dist) < m_spacing) {
		m_dragdist += new_dist;
		m_dragStart = pos;
		return;
	}
	else
		m_dragdist = 0;

	dragVec.normalize();
	KisVector step = start;

	while (dist >= m_spacing) {
		if (saved_dist > 0) {
			step += dragVec * (m_spacing - saved_dist);
			saved_dist -= m_spacing;
		}
		else
			step += dragVec * m_spacing;

		QPoint p(qRound(step.x()), qRound(step.y()));

		if (paintMonochrome(p)) {
			QRect rc(p - m_hotSpot, m_brushSize);

			img -> markDirty(rc);
		}

		dist -= m_spacing;
	}

	if (dist > 0) 
		m_dragdist = dist;

	m_dragStart = pos;
}

void BrushTool::mouseRelease(QMouseEvent *e)
{
	if (e -> button() != LeftButton)
		return;

	m_dragging = false;
}

bool BrushTool::paintColor(const QPoint& /*pos*/)
{
	return true;
}

void BrushTool::optionsDialog()
{
	ToolOptsStruct ts;

	ts.usePattern = m_usePattern;
	ts.useGradient = m_useGradient;
	ts.opacity = m_opacity;

	bool old_usePattern = m_usePattern;
	bool old_useGradient = m_useGradient;
	unsigned int old_opacity = m_opacity;

	ToolOptionsDialog OptsDialog(tt_brushtool, ts);

	OptsDialog.exec();

	if(OptsDialog.result() == QDialog::Rejected)
		return;

	m_opacity = OptsDialog.brushToolTab()->opacity();
	m_usePattern = OptsDialog.brushToolTab()->usePattern();
	m_useGradient = OptsDialog.brushToolTab()->useGradient();

	if (old_usePattern != m_usePattern || old_useGradient != m_useGradient || old_opacity != m_opacity)
		m_doc -> setModified(true);
}

void BrushTool::setupAction(QObject *collection)
{
	m_toggle = new KToggleAction(i18n("&Brush tool"), "paintbrush", 0, this, SLOT(toolSelect()), collection, "tool_brush");
	m_toggle -> setExclusiveGroup("tools");
}

QDomElement BrushTool::saveSettings(QDomDocument& doc) const
{
	QDomElement tool = doc.createElement("brushTool");

	tool.setAttribute("opacity", m_opacity);
	tool.setAttribute("blendWithCurrentGradient", static_cast<int>(m_useGradient));
	tool.setAttribute("blendWithCurrentPattern", static_cast<int>(m_usePattern));
	return tool;
}

bool BrushTool::loadSettings(QDomElement& elem)
{
	bool rc = elem.tagName() == "brushTool";

	if (rc) {
		m_opacity = elem.attribute("opacity").toInt();
		m_useGradient = static_cast<bool>(elem.attribute("blendWithCurrentGradient").toInt());
		m_usePattern = static_cast<bool>(elem.attribute("blendWithCurrentPattern").toInt());
	}

	return rc;
}

void BrushTool::toolSelect()
{
	if (m_view)
		m_view -> activateTool(this);

	m_toggle -> setChecked(true);
}

