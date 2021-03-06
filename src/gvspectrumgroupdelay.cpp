/*
Copyright (C) 2014  Gilles Degottex <gilles.degottex@gmail.com>

This file is part of DFasma.

DFasma is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

DFasma is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

A copy of the GNU General Public License is available in the LICENSE.txt
file provided in the source code of DFasma. Another copy can be found at
<http://www.gnu.org/licenses/>.
*/

#include "gvspectrumgroupdelay.h"

#include "wmainwindow.h"
#include "ui_wmainwindow.h"
#include "gvwaveform.h"
#include "gvspectrumamplitude.h"
#include "gvspectrumphase.h"
#include "gvspectrogram.h"
#include "ftsound.h"
#include "ftfzero.h"
#include "ui_wdialogsettings.h"

#include <iostream>
#include <algorithm>
using namespace std;

#include <QWheelEvent>
#include <QToolBar>
#include <QAction>
#include <QSpinBox>
#include <QGraphicsLineItem>
#include <QStaticText>
#include <QDebug>

#include "qaesigproc.h"
#include "qaehelpers.h"

GVSpectrumGroupDelay::GVSpectrumGroupDelay(WMainWindow* parent)
    : QGraphicsView(parent)
    , m_scene(NULL)
{
    setStyleSheet("QGraphicsView { border-style: none; }");
    setFrameShape(QFrame::NoFrame);
    setFrameShadow(QFrame::Plain);
    setHorizontalScrollBar(new QScrollBarHover(Qt::Horizontal, this));
    setVerticalScrollBar(new QScrollBarHover(Qt::Vertical, this));

    m_scene = new QGraphicsScene(this);
    setScene(m_scene);

    m_aSpectrumGroupDelayShowGrid = new QAction(tr("Show &grid"), this);
    m_aSpectrumGroupDelayShowGrid->setObjectName("m_aSpectrumGroupDelayShowGrid");
    m_aSpectrumGroupDelayShowGrid->setStatusTip(tr("Show or hide the grid"));
    m_aSpectrumGroupDelayShowGrid->setIcon(QIcon(":/icons/grid.svg"));
    m_aSpectrumGroupDelayShowGrid->setCheckable(true);
    m_aSpectrumGroupDelayShowGrid->setChecked(true);
    gMW->m_settings.add(m_aSpectrumGroupDelayShowGrid);
    m_giGrid = new QAEGIGrid(this, "Hz", "s");
    m_giGrid->setMathYAxis(true);
    m_giGrid->setFont(gMW->m_dlgSettings->ui->lblGridFontSample->font());
    m_giGrid->setVisible(m_aSpectrumGroupDelayShowGrid->isChecked());
    m_scene->addItem(m_giGrid);
    connect(m_aSpectrumGroupDelayShowGrid, SIGNAL(toggled(bool)), this, SLOT(gridSetVisible(bool)));

    // Cursor
    m_giCursorHoriz = new QGraphicsLineItem(0, -100, 0, 100);
    m_giCursorHoriz->hide();
    QPen cursorPen(QColor(64, 64, 64));
    cursorPen.setWidth(0);
    m_giCursorHoriz->setPen(cursorPen);
    m_giCursorHoriz->setZValue(100);
    m_scene->addItem(m_giCursorHoriz);
    m_giCursorVert = new QGraphicsLineItem(0, 0, gFL->getFs()/2.0, 0);
    m_giCursorVert->hide();
    m_giCursorVert->setPen(cursorPen);
    m_giCursorVert->setZValue(100);
    m_scene->addItem(m_giCursorVert);
    m_giCursorPositionXTxt = new QGraphicsSimpleTextItem();
    m_giCursorPositionXTxt->hide();
    m_giCursorPositionXTxt->setBrush(QColor(64, 64, 64));
    m_giCursorPositionXTxt->setFont(gMW->m_dlgSettings->ui->lblGridFontSample->font());
    m_giCursorPositionXTxt->setZValue(100);
    m_scene->addItem(m_giCursorPositionXTxt);
    m_giCursorPositionYTxt = new QGraphicsSimpleTextItem();
    m_giCursorPositionYTxt->hide();
    m_giCursorPositionYTxt->setBrush(QColor(64, 64, 64));
    m_giCursorPositionYTxt->setFont(gMW->m_dlgSettings->ui->lblGridFontSample->font());
    m_giCursorPositionYTxt->setZValue(100);
    m_scene->addItem(m_giCursorPositionYTxt);

    // Selection
    m_currentAction = CANothing;
    m_giShownSelection = new QGraphicsRectItem();
    m_giShownSelection->hide();
    m_giShownSelection->setZValue(100.0);
    m_scene->addItem(m_giShownSelection);
    m_giSelectionTxt = new QGraphicsSimpleTextItem();
    m_giSelectionTxt->hide();
    m_giSelectionTxt->setBrush(QColor(64, 64, 64));
    m_giSelectionTxt->setFont(gMW->m_dlgSettings->ui->lblGridFontSample->font());
    m_scene->addItem(m_giSelectionTxt);
    QPen selectionPen(QColor(64, 64, 64));
    selectionPen.setWidth(0);
    QBrush selectionBrush(QColor(192, 192, 192));
    m_giShownSelection->setPen(selectionPen);
    m_giShownSelection->setBrush(selectionBrush);
    m_giShownSelection->setOpacity(0.5);
    gMW->ui->lblSpectrumSelectionTxt->setText("No selection");

    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setMouseTracking(true);

    // Build the context menu
    m_contextmenu.addAction(m_aSpectrumGroupDelayShowGrid);
    m_contextmenu.addSeparator();
    m_contextmenu.addAction(gMW->m_gvSpectrumAmplitude->m_aAutoUpdateDFT);
    m_contextmenu.addAction(gMW->m_gvSpectrumAmplitude->m_aFollowPlayCursor);
    m_contextmenu.addSeparator();
    m_contextmenu.addAction(gMW->m_gvSpectrumAmplitude->m_aShowProperties);

    connect(gMW->m_gvWaveform->m_aWaveformShowSelectedWaveformOnTop, SIGNAL(triggered()), m_scene, SLOT(update()));
}

void GVSpectrumGroupDelay::updateSceneRect() {
//    COUTD << "GVSpectrumGroupDelay::updateSceneRect" << std::endl;

    QRectF currentviewrect = mapToScene(viewport()->rect()).boundingRect();

    double maxdelay = gFL->getMaxDuration()/2;
    if(gMW->m_gvSpectrumAmplitude->m_trgDFTParameters.winlen>0)
        maxdelay = ((gMW->m_gvSpectrumAmplitude->m_trgDFTParameters.winlen-1)/2)/gFL->getFs();

//    COUTD << maxdelay << endl;

    m_scene->setSceneRect(0.0, -maxdelay, gFL->getFs()/2, 2*maxdelay);

    if(currentviewrect.top()<-maxdelay)
        currentviewrect.setTop(-maxdelay);
    if(currentviewrect.bottom()>maxdelay)
        currentviewrect.setBottom(maxdelay);

    viewSet(currentviewrect, false);
}

void GVSpectrumGroupDelay::viewSet(QRectF viewrect, bool sync) {
//    COUTD << "QGVSpectrumGroupDelay::viewSet " << viewrect << endl;

    QRectF currentviewrect = mapToScene(viewport()->rect()).boundingRect();

    if(viewrect==QRect() || viewrect!=currentviewrect) {
        if(viewrect==QRectF())
            viewrect = currentviewrect;

        QPointF center = viewrect.center();
        double hzeps = 1e-10;
        if(viewrect.width()<hzeps){
            viewrect.setLeft(center.x()-0.5*hzeps);
            viewrect.setRight(center.x()+0.5*hzeps);
        }
        double seps = 0.1*1.0/gFL->getFs();
        if(viewrect.height()<seps){
            viewrect.setTop(center.x()-0.5*seps);
            viewrect.setBottom(center.x()+0.5*seps);
        }

        if(viewrect.top()<=m_scene->sceneRect().top())
            viewrect.setTop(m_scene->sceneRect().top());
        if(viewrect.bottom()>=m_scene->sceneRect().bottom())
            viewrect.setBottom(m_scene->sceneRect().bottom());
        if(viewrect.left()<m_scene->sceneRect().left())
            viewrect.setLeft(m_scene->sceneRect().left());
        if(viewrect.right()>m_scene->sceneRect().right())
            viewrect.setRight(m_scene->sceneRect().right());

        fitInView(removeHiddenMargin(this, viewrect));

        m_giGrid->updateLines();

        if(sync){
            if(gMW->m_gvSpectrumAmplitude && gMW->ui->actionShowAmplitudeSpectrum->isChecked()) {
                QRectF amprect = gMW->m_gvSpectrumAmplitude->mapToScene(gMW->m_gvSpectrumAmplitude->viewport()->rect()).boundingRect();
                amprect.setLeft(viewrect.left());
                amprect.setRight(viewrect.right());
                gMW->m_gvSpectrumAmplitude->viewSet(amprect, false);
            }

            if(gMW->m_gvSpectrumPhase && gMW->ui->actionShowPhaseSpectrum->isChecked()) {
                QRectF phaserect = gMW->m_gvSpectrumPhase->mapToScene(gMW->m_gvSpectrumPhase->viewport()->rect()).boundingRect();
                phaserect.setLeft(viewrect.left());
                phaserect.setRight(viewrect.right());
                gMW->m_gvSpectrumPhase->viewSet(phaserect, false);
            }
        }
    }
//    cout << "QGVSpectrumGroupDelay::~viewSet" << endl;
}

void GVSpectrumGroupDelay::resizeEvent(QResizeEvent* event) {
//    COUTD << "QGVSpectrumGroupDelay::resizeEvent" << endl;

    QRectF oldviewrect = mapToScene(QRect(QPoint(0,0), event->oldSize())).boundingRect();

    if(event->oldSize().isEmpty() && !event->size().isEmpty()) {

        updateSceneRect();
        gMW->m_gvSpectrumAmplitude->updateDFTs();

        if(gMW->m_gvSpectrumAmplitude->viewport()->rect().width()*gMW->m_gvSpectrumAmplitude->viewport()->rect().height()>0){
            QRectF amprect = gMW->m_gvSpectrumAmplitude->mapToScene(gMW->m_gvSpectrumAmplitude->viewport()->rect()).boundingRect();

            QRectF viewrect;
            viewrect.setLeft(amprect.left());
            viewrect.setRight(amprect.right());
            viewrect.setTop(sceneRect().top());
            viewrect.setBottom(sceneRect().bottom());
            viewSet(viewrect, false);
        }
        else
            viewSet(m_scene->sceneRect(), false);
    }
    else if(!event->oldSize().isEmpty() && !event->size().isEmpty())
    {
        viewSet(oldviewrect, false);
    }

    viewUpdateTexts();
    setMouseCursorPosition(QPointF(-1,0), false);

//    COUTD << "QGVSpectrumGroupDelay::~resizeEvent" << endl;
}

void GVSpectrumGroupDelay::scrollContentsBy(int dx, int dy){

    setMouseCursorPosition(QPointF(-1,0), false);
    viewUpdateTexts();

    QGraphicsView::scrollContentsBy(dx, dy);

    m_giGrid->updateLines();
}

void GVSpectrumGroupDelay::wheelEvent(QWheelEvent* event) {
//    cout << "QGVSpectrumGroupDelay::wheelEvent" << endl;

    qreal numDegrees = (event->angleDelta() / 8).y();
    // Clip to avoid flipping (workaround of a Qt bug ?)
    if(numDegrees>90) numDegrees = 90;
    if(numDegrees<-90) numDegrees = -90;

    QTransform trans = transform();
    qreal h11 = trans.m11();
    qreal h22 = trans.m22();
    h11 += 0.01*h11*numDegrees;
    h22 += 0.01*h22*numDegrees;
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setTransform(QTransform(h11, trans.m12(), trans.m21(), h22, 0, 0));
    viewSet();

    setMouseCursorPosition(mapToScene(event->pos()), true);

//    cout << "QGVSpectrumGroupDelay::~wheelEvent" << endl;
}

void GVSpectrumGroupDelay::mousePressEvent(QMouseEvent* event){
//    std::cout << "QGVWaveform::mousePressEvent" << endl;

    bool kshift = event->modifiers().testFlag(Qt::ShiftModifier);
    bool kctrl = event->modifiers().testFlag(Qt::ControlModifier);

    QPointF p = mapToScene(event->pos());
    QRect selview = mapFromScene(m_selection).boundingRect();

    if(event->buttons()&Qt::LeftButton){
        if(gMW->ui->actionSelectionMode->isChecked()){
            if(kshift && !kctrl) {
                // When moving the spectrum's view
                m_currentAction = CAMoving;
                setMouseCursorPosition(QPointF(-1,0), false);
            }
            else if((!kctrl && !kshift) && m_selection.width()>0 && m_selection.height()>0 && abs(selview.left()-event->x())<5 && event->y()>=selview.top() && event->y()<=selview.bottom()) {
                // Resize left boundary of the selection
                m_currentAction = CAModifSelectionLeft;
                m_selection_pressedp = QPointF(p.x()-m_selection.left(), 0);
            }
            else if((!kctrl && !kshift) && m_selection.width()>0 && m_selection.height()>0 && abs(selview.right()-event->x())<5 && event->y()>=selview.top() && event->y()<=selview.bottom()){
                // Resize right boundary of the selection
                m_currentAction = CAModifSelectionRight;
                m_selection_pressedp = QPointF(p.x()-m_selection.right(), 0);
            }
            else if((!kctrl && !kshift) && m_selection.width()>0 && m_selection.height()>0 && abs(selview.top()-event->y())<5 && event->x()>=selview.left() && event->x()<=selview.right()){
                // Resize top boundary of the selection
                m_currentAction = CAModifSelectionTop;
                m_selection_pressedp = QPointF(0, p.y()-m_selection.top());
            }
            else if((!kctrl && !kshift) && m_selection.width()>0 && m_selection.height()>0 && abs(selview.bottom()-event->y())<5 && event->x()>=selview.left() && event->x()<=selview.right()){
                // Resize bottom boundary of the selection
                m_currentAction = CAModifSelectionBottom;
                m_selection_pressedp = QPointF(0, p.y()-m_selection.bottom());
            }
            else if((m_selection.width()>0 && m_selection.height()>0) && ((kctrl && !kshift) || (p.x()>=m_selection.left() && p.x()<=m_selection.right() && p.y()>=m_selection.top() && p.y()<=m_selection.bottom()))){
                // When scroling the selection
                m_currentAction = CAMovingSelection;
                m_selection_pressedp = p;
                m_mouseSelection = m_selection;
                setCursor(Qt::ClosedHandCursor);
        //            gMW->ui->lblSpectrumSelectionTxt->setText(QString("Selection [%1s").arg(m_selection.left()).append(",%1s] ").arg(m_selection.right()).append("%1s").arg(m_selection.width()));
            }
            else if(kctrl && kshift) {
                // cout << "Move waveform's selection" << endl;
                m_currentAction = CAMovingWaveformSelection;
                m_selection_pressedp = p;
                m_wavselection_pressed = gMW->m_gvWaveform->m_selection;
            }
            else {
                // When selecting
                m_currentAction = CASelecting;
                m_selection_pressedp = p;
                m_mouseSelection.setTopLeft(m_selection_pressedp);
                m_mouseSelection.setBottomRight(m_selection_pressedp);
                selectionSet(m_mouseSelection, true);
            }
        }
        else if(gMW->ui->actionEditMode->isChecked()
                && (gFL->currentFile() && gFL->currentFile()->isVisible())){
            if(!kctrl && !kshift) {
                FTSound* currentftsound = gFL->getCurrentFTSound(false);
                if(currentftsound){
                    m_currentAction = CAWaveformDelay;
                    m_selection_pressedp = p;
                    m_pressed_delay = currentftsound->m_giWavForWaveform->delay();
                    gMW->setEditing(currentftsound);
                }
            }
        }
    }
    else if(event->buttons()&Qt::RightButton) {
        if (kshift && !kctrl) {
            setCursor(Qt::CrossCursor);
            m_currentAction = CAZooming;
            m_selection_pressedp = p;
            m_pressed_mouseinviewport = mapFromScene(p);
            m_pressed_viewrect = mapToScene(viewport()->rect()).boundingRect();
        }
    }

    QGraphicsView::mousePressEvent(event);
//    std::cout << "~QGVWaveform::mousePressEvent " << p.x() << endl;
}

void GVSpectrumGroupDelay::mouseMoveEvent(QMouseEvent* event){
//    std::cout << "QGVWaveform::mouseMoveEvent" << selection.width() << endl;

    QPointF p = mapToScene(event->pos());

    setMouseCursorPosition(p, true);

//    std::cout << "QGVWaveform::mouseMoveEvent action=" << m_currentAction << " x=" << p.x() << " y=" << p.y() << endl;

    if(m_currentAction==CAMoving) {
        // When scrolling the view around the scene
//        setMouseCursorPosition(QPointF(-1,0), true);
        m_giGrid->updateLines();
    }
    else if(m_currentAction==CAZooming) {
        double dx = -(event->pos()-m_pressed_mouseinviewport).x()/100.0;
        double dy = (event->pos()-m_pressed_mouseinviewport).y()/100.0;

        QRectF newrect = m_pressed_viewrect;
        newrect.setLeft(m_selection_pressedp.x()-(m_selection_pressedp.x()-m_pressed_viewrect.left())*exp(dx));
        newrect.setRight(m_selection_pressedp.x()+(m_pressed_viewrect.right()-m_selection_pressedp.x())*exp(dx));
        newrect.setTop(m_selection_pressedp.y()-(m_selection_pressedp.y()-m_pressed_viewrect.top())*exp(dy));
        newrect.setBottom(m_selection_pressedp.y()+(m_pressed_viewrect.bottom()-m_selection_pressedp.y())*exp(dy));
        viewSet(newrect);

        setMouseCursorPosition(m_selection_pressedp, true);
    }
    else if(m_currentAction==CAModifSelectionLeft){
        m_mouseSelection.setLeft(p.x()-m_selection_pressedp.x());
        selectionSet(m_mouseSelection, true);
    }
    else if(m_currentAction==CAModifSelectionRight){
        m_mouseSelection.setRight(p.x()-m_selection_pressedp.x());
        selectionSet(m_mouseSelection, true);
    }
    else if(m_currentAction==CAModifSelectionTop){
        m_mouseSelection.setTop(p.y()-m_selection_pressedp.y());
        selectionSet(m_mouseSelection, true);
    }
    else if(m_currentAction==CAModifSelectionBottom){
        m_mouseSelection.setBottom(p.y()-m_selection_pressedp.y());
        selectionSet(m_mouseSelection, true);
    }
    else if(m_currentAction==CAMovingSelection){
        // When scroling the selection
        m_mouseSelection.adjust(p.x()-m_selection_pressedp.x(), p.y()-m_selection_pressedp.y(), p.x()-m_selection_pressedp.x(), p.y()-m_selection_pressedp.y());
        selectionSet(m_mouseSelection, true);
        m_selection_pressedp = p;
    }
    else if(m_currentAction==CASelecting){
        // When selecting
        m_mouseSelection.setTopLeft(m_selection_pressedp);
        m_mouseSelection.setBottomRight(p);
        selectionSet(m_mouseSelection, true);
    }
    else if(m_currentAction==CAMovingWaveformSelection) {
        double dt = (p.y()-m_selection_pressedp.y());
        QRectF wavsel = m_wavselection_pressed;
        wavsel.setLeft(wavsel.left()+dt);
        wavsel.setRight(wavsel.right()+dt);
        gMW->m_gvWaveform->selectionSet(wavsel);
    }
    else if(m_currentAction==CAWaveformDelay) {
        double dy = -(p.y()-m_selection_pressedp.y());
        // cout << "CAWaveformDelay at " << m_selection_pressedp.x() << " " << dy << "s" << endl;
        double dt = ((gFL->getFs()/m_selection_pressedp.x())*dy/(sceneRect().height()))/gFL->getFs();
        FTSound* currentftsound = gFL->getCurrentFTSound(true);
        if(currentftsound){
            currentftsound->m_giWavForWaveform->setDelay(m_pressed_delay + dt*gFL->getFs());

            currentftsound->needDFTUpdate();

            gMW->m_gvWaveform->m_scene->update();
            gMW->m_gvSpectrumAmplitude->updateDFTs();
            gFL->fileInfoUpdate();
            gMW->ui->pbSpectrogramSTFTUpdate->show();
        }
    }
    else{
        QRect selview = mapFromScene(m_selection).boundingRect();

        if(gMW->ui->actionSelectionMode->isChecked()){
            if(event->modifiers().testFlag(Qt::ShiftModifier)){
            }
            else if(event->modifiers().testFlag(Qt::ControlModifier)){
            }
            else{
                if(m_selection.width()>0 && m_selection.height()>0 && abs(selview.left()-event->x())<5 && event->y()>=selview.top() && event->y()<=selview.bottom())
                    setCursor(Qt::SplitHCursor);
                else if(m_selection.width()>0 && m_selection.height()>0 && abs(selview.right()-event->x())<5 && selview.top()<=event->y() && selview.bottom()>=event->y())
                    setCursor(Qt::SplitHCursor);
                else if(m_selection.width()>0 && m_selection.height()>0 && abs(selview.top()-event->y())<5 && event->x()>=selview.left() && event->x()<=selview.right())
                    setCursor(Qt::SplitVCursor);
                else if(m_selection.width()>0 && m_selection.height()>0 && abs(selview.bottom()-event->y())<5 && event->x()>=selview.left() && event->x()<=selview.right())
                    setCursor(Qt::SplitVCursor);
                else if(p.x()>=m_selection.left() && p.x()<=m_selection.right() && p.y()>=m_selection.top() && p.y()<=m_selection.bottom())
                    setCursor(Qt::OpenHandCursor);
                else
                    setCursor(Qt::CrossCursor);
            }
        }
        else if(gMW->ui->actionEditMode->isChecked()){
            if(event->modifiers().testFlag(Qt::ShiftModifier)){
            }
            else if(event->modifiers().testFlag(Qt::ControlModifier)){
            }
            else{
            }
        }
    }

//    std::cout << "~QGVWaveform::mouseMoveEvent" << endl;
    QGraphicsView::mouseMoveEvent(event);
}

void GVSpectrumGroupDelay::mouseReleaseEvent(QMouseEvent* event){
//    std::cout << "QGVWaveform::mouseReleaseEvent " << selection.width() << endl;

    bool kshift = event->modifiers().testFlag(Qt::ShiftModifier);
    bool kctrl = event->modifiers().testFlag(Qt::ControlModifier);

    m_currentAction = CANothing;

    gMW->updateMouseCursorState(kshift, kctrl);

    QPointF p = mapToScene(event->pos());
    if(gMW->ui->actionSelectionMode->isChecked()){
        if(abs(m_selection.width())<=0 || abs(m_selection.height())<=0)
            gMW->m_gvSpectrumAmplitude->selectionClear();

        if(!kshift && !kctrl){
            if(p.x()>=m_selection.left() && p.x()<=m_selection.right() && p.y()>=m_selection.top() && p.y()<=m_selection.bottom())
                setCursor(Qt::OpenHandCursor);
        }
    }

    QGraphicsView::mouseReleaseEvent(event);
//    std::cout << "~QGVWaveform::mouseReleaseEvent " << endl;
}

void GVSpectrumGroupDelay::keyPressEvent(QKeyEvent* event){
    if(event->key()==Qt::Key_Escape){
        if(!gMW->m_gvSpectrumAmplitude->hasSelection()) {
            if(!gMW->m_gvSpectrogram->hasSelection()
                && !gMW->m_gvWaveform->hasSelection())
                gMW->m_gvWaveform->playCursorSet(0.0, true);

            gMW->m_gvSpectrogram->selectionClear();
        }
        gMW->m_gvSpectrumAmplitude->selectionClear();
    }
    if(event->key()==Qt::Key_S)
        selectionZoomOn();

    QGraphicsView::keyPressEvent(event);
}

void GVSpectrumGroupDelay::contextMenuEvent(QContextMenuEvent *event){
    if (event->modifiers().testFlag(Qt::ShiftModifier)
        || event->modifiers().testFlag(Qt::ControlModifier))
        return;

    QPoint posglobal = mapToGlobal(event->pos()+QPoint(0,0));
    m_contextmenu.exec(posglobal);
}

void GVSpectrumGroupDelay::selectionClear(bool forwardsync){
    Q_UNUSED(forwardsync)
    m_selection = QRectF(0, 0, 0, 0);
    m_mouseSelection = QRectF(0, 0, 0, 0);
    m_giShownSelection->hide();
    m_giShownSelection->setRect(QRectF(0, 0, 0, 0));
    m_giSelectionTxt->hide();
    setCursor(Qt::CrossCursor);
}

void GVSpectrumGroupDelay::selectionSet(QRectF selection, bool forwardsync){
    // Order the selection to avoid negative width and negative height
    if(selection.right()<selection.left()){
        qreal tmp = selection.left();
        selection.setLeft(selection.right());
        selection.setRight(tmp);
    }
    if(selection.top()>selection.bottom()){
        qreal tmp = selection.top();
        selection.setTop(selection.bottom());
        selection.setBottom(tmp);
    }

    m_selection = m_mouseSelection = selection;

    if(m_selection.left()<0) m_selection.setLeft(0);
    if(m_selection.right()>gFL->getFs()/2.0) m_selection.setRight(gFL->getFs()/2.0);
    if(m_selection.top()<m_scene->sceneRect().top()) m_selection.setTop(m_scene->sceneRect().top());
    if(m_selection.bottom()>m_scene->sceneRect().bottom()) m_selection.setBottom(m_scene->sceneRect().bottom());

    m_giShownSelection->setRect(m_selection);
    m_giShownSelection->show();

    m_giSelectionTxt->setText(QString("%1Hz,%2s").arg(m_selection.width()).arg(m_selection.height()));
//    m_giSelectionTxt->show();
    viewUpdateTexts();

    if(forwardsync) {
        if(gMW->m_gvSpectrumAmplitude){
            QRectF rect = gMW->m_gvSpectrumAmplitude->m_mouseSelection;
            rect.setLeft(m_mouseSelection.left());
            rect.setRight(m_mouseSelection.right());
            if(rect.height()==0) {
                rect.setTop(gMW->m_gvSpectrumAmplitude->m_scene->sceneRect().top());
                rect.setBottom(gMW->m_gvSpectrumAmplitude->m_scene->sceneRect().bottom());
            }
            gMW->m_gvSpectrumAmplitude->selectionSet(rect, false);
        }

        if(gMW->m_gvSpectrumPhase){
            QRectF rect = gMW->m_gvSpectrumPhase->m_mouseSelection;
            rect.setLeft(m_mouseSelection.left());
            rect.setRight(m_mouseSelection.right());
            if(rect.height()==0) {
                rect.setTop(gMW->m_gvSpectrumPhase->m_scene->sceneRect().top());
                rect.setBottom(gMW->m_gvSpectrumPhase->m_scene->sceneRect().bottom());
            }
            gMW->m_gvSpectrumPhase->selectionSet(rect, false);
        }

        if(gMW->m_gvSpectrogram){
            QRectF rect = gMW->m_gvSpectrogram->m_mouseSelection;
            rect.setTop(gFL->getFs()/2-m_mouseSelection.right());
            rect.setBottom(gFL->getFs()/2-m_mouseSelection.left());
            if(!gMW->m_gvSpectrogram->m_giShownSelection->isVisible()) {
                rect.setLeft(gMW->m_gvSpectrogram->m_scene->sceneRect().left());
                rect.setRight(gMW->m_gvSpectrogram->m_scene->sceneRect().right());
            }
            gMW->m_gvSpectrogram->selectionSet(rect, false);
        }
    }

}

void GVSpectrumGroupDelay::viewUpdateTexts(){
    QTransform trans = transform();
    QTransform txttrans;
    txttrans.scale(1.0/trans.m11(), 1.0/trans.m22());

    // Cursor
    m_giCursorPositionXTxt->setTransform(txttrans);
    m_giCursorPositionYTxt->setTransform(txttrans);

    // Selection
    QRectF br = m_giSelectionTxt->boundingRect();
    m_giSelectionTxt->setTransform(txttrans);
    m_giSelectionTxt->setPos(m_selection.center()-QPointF(0.5*br.width()/trans.m11(), 0.5*br.height()/trans.m22()));
}

void GVSpectrumGroupDelay::selectionZoomOn(){
    if(m_selection.width()>0 && m_selection.height()>0){
        QRectF zoomonrect = m_selection;
        if(gMW->m_dlgSettings->ui->cbViewsAddMarginsOnSelection->isChecked()) {
            zoomonrect.setTop(zoomonrect.top()-0.1*zoomonrect.height());
            zoomonrect.setBottom(zoomonrect.bottom()+0.1*zoomonrect.height());
            zoomonrect.setLeft(zoomonrect.left()-0.1*zoomonrect.width());
            zoomonrect.setRight(zoomonrect.right()+0.1*zoomonrect.width());
        }
        viewSet(zoomonrect);

        viewUpdateTexts();

        if(gMW->m_gvSpectrumPhase)
            gMW->m_gvSpectrumPhase->viewUpdateTexts();
        if(gMW->m_gvSpectrumAmplitude)
            gMW->m_gvSpectrumAmplitude->viewUpdateTexts();

        setMouseCursorPosition(QPointF(-1,0), false);
    }
}

void GVSpectrumGroupDelay::azoomin(){
    QTransform trans = transform();
    qreal h11 = trans.m11();
    qreal h22 = trans.m22();
    h11 *= 1.5;
    h22 *= 1.5;
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    setTransform(QTransform(h11, trans.m12(), trans.m21(), h22, 0, 0));
    viewSet();

    setMouseCursorPosition(QPointF(-1,0), false);
}
void GVSpectrumGroupDelay::azoomout(){
    QTransform trans = transform();
    qreal h11 = trans.m11();
    qreal h22 = trans.m22();
    h11 /= 1.5;
    h22 /= 1.5;
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    setTransform(QTransform(h11, trans.m12(), trans.m21(), h22, 0, 0));
    viewSet();

    setMouseCursorPosition(QPointF(-1,0), false);
}

void GVSpectrumGroupDelay::setMouseCursorPosition(QPointF p, bool forwardsync) {

    QFontMetrics qfm(gMW->m_dlgSettings->ui->lblGridFontSample->font());

    QLineF line;
    line.setP1(QPointF(p.x(), m_giCursorVert->line().y1()));
    line.setP2(QPointF(p.x(), m_giCursorVert->line().y2()));
    m_giCursorVert->setLine(line);
    line.setP1(QPointF(m_giCursorHoriz->line().x1(), p.y()));
    line.setP2(QPointF(m_giCursorHoriz->line().x2(), p.y()));
    m_giCursorHoriz->setLine(line);
    if(m_giCursorVert->line().x1()==-1){
        m_giCursorHoriz->hide();
        m_giCursorVert->hide();
        m_giCursorPositionXTxt->hide();
        m_giCursorPositionYTxt->hide();
    }
    else {
        QTransform trans = transform();
        QRectF viewrect = mapToScene(viewport()->rect()).boundingRect();
        QTransform txttrans;
        txttrans.scale(1.0/trans.m11(),1.0/trans.m22());

        m_giCursorHoriz->show();
        m_giCursorHoriz->setLine(viewrect.right()-50/trans.m11(), m_giCursorHoriz->line().y1(), gFL->getFs()/2.0, m_giCursorHoriz->line().y1());

        m_giCursorVert->show();
        m_giCursorVert->setLine(m_giCursorVert->line().x1(), viewrect.top(), m_giCursorVert->line().x1(), viewrect.top()+(qfm.height())/trans.m22());

        m_giCursorPositionXTxt->setFont(gMW->m_dlgSettings->ui->lblGridFontSample->font());
        m_giCursorPositionYTxt->setFont(gMW->m_dlgSettings->ui->lblGridFontSample->font());

        m_giCursorPositionXTxt->show();
        m_giCursorPositionXTxt->setTransform(txttrans);
        m_giCursorPositionXTxt->setText(QString("%1Hz").arg(m_giCursorVert->line().x1()));
        QRectF br = m_giCursorPositionXTxt->boundingRect();
        qreal x = m_giCursorVert->line().x1()+1/trans.m11();
        x = min(x, viewrect.right()-br.width()/trans.m11());
        m_giCursorPositionXTxt->setPos(x, viewrect.top());

        m_giCursorPositionYTxt->show();
        m_giCursorPositionYTxt->setTransform(txttrans);
        m_giCursorPositionYTxt->setText(QString("%1s").arg(-m_giCursorHoriz->line().y1()));
        br = m_giCursorPositionYTxt->boundingRect();
        m_giCursorPositionYTxt->setPos(viewrect.right()-br.width()/trans.m11(), m_giCursorHoriz->line().y1()-br.height()/trans.m22());
    }

    if(forwardsync){
        if(gMW->m_gvSpectrogram)
            gMW->m_gvSpectrogram->setMouseCursorPosition(QPointF(gMW->m_gvSpectrogram->m_giMouseCursorLineTime->x(), -p.x()), false);
        if(gMW->m_gvSpectrumAmplitude)
            gMW->m_gvSpectrumAmplitude->setMouseCursorPosition(QPointF(p.x(), 0.0), false);
        if(gMW->m_gvSpectrumPhase)
            gMW->m_gvSpectrumPhase->setMouseCursorPosition(QPointF(p.x(), 0.0), false);
    }
}

void GVSpectrumGroupDelay::drawBackground(QPainter* painter, const QRectF& rect){
    Q_UNUSED(rect)

//    COUTD << ": QGVSpectrumGroupDelay::drawBackground " << rect.left() << " " << rect.right() << " " << rect.top() << " " << rect.bottom() << endl;

    // QGraphicsView::drawBackground(painter, rect);// TODO Need this ??

    // Draw the f0 grids
    if(!gFL->ftfzeros.empty()) {

        for(size_t fi=0; fi<gFL->ftfzeros.size(); fi++){
            if(!gFL->ftfzeros[fi]->m_actionShow->isChecked())
                continue;

            QPen outlinePen(gFL->ftfzeros[fi]->getColor());
            outlinePen.setWidth(0);
            painter->setPen(outlinePen);
            painter->setBrush(QBrush(gFL->ftfzeros[fi]->getColor()));

            double ct = 0.0; // The time where the f0 curve has to be sampled
            if(gMW->m_gvWaveform->hasSelection())
                ct = 0.5*(gMW->m_gvSpectrumAmplitude->m_trgDFTParameters.nl+gMW->m_gvSpectrumAmplitude->m_trgDFTParameters.nr)/gFL->getFs();
            else
                ct = gMW->m_gvWaveform->getPlayCursorPosition();
            double cf0 = qae::interp_stepatzeros<double>(gFL->ftfzeros[fi]->ts, gFL->ftfzeros[fi]->f0s, ct);

            // cout << ct << ":" << cf0 << endl;
            if(cf0<=0.0)
                continue;

            QColor c = gFL->ftfzeros[fi]->getColor();
            c.setAlphaF(1.0);
            outlinePen.setColor(c);
            painter->setPen(outlinePen);
            painter->drawLine(QLineF(cf0, sceneRect().top(), cf0, sceneRect().bottom()));

            c.setAlphaF(0.5);
            outlinePen.setColor(c);
            painter->setPen(outlinePen);

            for(int h=2; h<int(0.5*gFL->getFs()/cf0)+1; h++)
                painter->drawLine(QLineF(h*cf0, sceneRect().top(), h*cf0, sceneRect().bottom()));
        }
    }

//    cout << "QGVSpectrumGroupDelay::~drawBackground" << endl;
}

GVSpectrumGroupDelay::~GVSpectrumGroupDelay() {
}
