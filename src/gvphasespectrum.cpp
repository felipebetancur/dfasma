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

#include "gvphasespectrum.h"

#include "wmainwindow.h"
#include "ui_wmainwindow.h"
#include "gvwaveform.h"
#include "gvamplitudespectrum.h"
#include "gvspectrogram.h"
#include "ftsound.h"
#include "ftfzero.h"
#include "sigproc.h"

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

#include "qthelper.h"

QGVPhaseSpectrum::QGVPhaseSpectrum(WMainWindow* parent)
    : QGraphicsView(parent)
    , m_scene(NULL)
{
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);

    //    m_dlgSettings = new GVPhaseSpectrumWDialogSettings(this);

    // Load the settings
    QSettings settings;
//    m_dlgSettings->ui->sbSpectrumAmplitudeRangeMin->setValue(settings.value("qgvphasespectrum/sbSpectrumAmplitudeRangeMin", -215).toInt());
//    m_dlgSettings->ui->sbSpectrumAmplitudeRangeMax->setValue(settings.value("qgvphasespectrum/sbSpectrumAmplitudeRangeMax", 10).toInt());
//    m_dlgSettings->ui->sbSpectrumOversamplingFactor->setValue(settings.value("qgvphasespectrum/sbSpectrumOversamplingFactor", 1).toInt());

    // Cursor
    m_giCursorHoriz = new QGraphicsLineItem(0, -100, 0, 100);
    QPen cursorPen(QColor(64, 64, 64));
    cursorPen.setWidth(0);
    m_giCursorHoriz->setPen(cursorPen);
    m_giCursorHoriz->hide();
    m_scene->addItem(m_giCursorHoriz);
    m_giCursorVert = new QGraphicsLineItem(0, 0, gMW->getFs()/2.0, 0);
    m_giCursorVert->setPen(cursorPen);
    m_giCursorVert->hide();
    m_scene->addItem(m_giCursorVert);
    QFont font("Helvetica", 10);
    m_giCursorPositionXTxt = new QGraphicsSimpleTextItem();
    m_giCursorPositionXTxt->setBrush(QColor(64, 64, 64));
    m_giCursorPositionXTxt->setFont(font);
    m_giCursorPositionXTxt->hide();
    m_scene->addItem(m_giCursorPositionXTxt);
    m_giCursorPositionYTxt = new QGraphicsSimpleTextItem();
    m_giCursorPositionYTxt->setBrush(QColor(64, 64, 64));
    m_giCursorPositionYTxt->setFont(font);
    m_giCursorPositionYTxt->hide();
    m_scene->addItem(m_giCursorPositionYTxt);

    // Selection
    m_currentAction = CANothing;
    m_giShownSelection = new QGraphicsRectItem();
    m_giShownSelection->hide();
    m_scene->addItem(m_giShownSelection);
    m_giSelectionTxt = new QGraphicsSimpleTextItem();
    m_giSelectionTxt->hide();
    m_giSelectionTxt->setBrush(QColor(64, 64, 64));
    m_giSelectionTxt->setFont(font);
    m_scene->addItem(m_giSelectionTxt);
    QPen selectionPen(QColor(64, 64, 64));
    selectionPen.setWidth(0);
    QBrush selectionBrush(QColor(192, 192, 192));
    m_giShownSelection->setPen(selectionPen);
    m_giShownSelection->setBrush(selectionBrush);
    m_giShownSelection->setOpacity(0.5);
    gMW->ui->lblSpectrumSelectionTxt->setText("No selection");

    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setMouseTracking(true);

    // Build the context menu
    m_contextmenu.addAction(gMW->m_gvAmplitudeSpectrum->m_aAmplitudeSpectrumShowGrid);
//    m_contextmenu.addAction(gMW->m_gvWaveform->m_aShowSelectedWaveformOnTop);
    m_contextmenu.addSeparator();
//    m_aShowProperties = new QAction(tr("&Properties"), this);
//    m_aShowProperties->setStatusTip(tr("Open the properties configuration panel of the spectrum view"));
//    m_contextmenu.addAction(m_aShowProperties);
//    connect(m_aShowProperties, SIGNAL(triggered()), m_dlgSettings, SLOT(exec()));
//    connect(m_dlgSettings, SIGNAL(accepted()), this, SLOT(updateSceneRect()));

    connect(gMW->m_gvAmplitudeSpectrum->m_aAmplitudeSpectrumShowGrid, SIGNAL(toggled(bool)), m_scene, SLOT(update()));
    connect(gMW->m_gvWaveform->m_aWaveformShowSelectedWaveformOnTop, SIGNAL(triggered()), m_scene, SLOT(update()));
}

// Remove hard coded margin (Bug 11945)
// See: https://bugreports.qt-project.org/browse/QTBUG-11945
QRectF QGVPhaseSpectrum::removeHiddenMargin(const QRectF& sceneRect){
    const int bugMargin = 2;
    const double mx = sceneRect.width()/viewport()->size().width()*bugMargin;
    const double my = sceneRect.height()/viewport()->size().height()*bugMargin;
    return sceneRect.adjusted(mx, my, -mx, -my);
}

void QGVPhaseSpectrum::updateSceneRect() {
    m_scene->setSceneRect(0.0, -M_PI, gMW->getFs()/2, 2*M_PI);
}

void QGVPhaseSpectrum::viewSet(QRectF viewrect, bool sync) {
//    cout << "QGVPhaseSpectrum::viewSet" << endl;

    QRectF currentviewrect = mapToScene(viewport()->rect()).boundingRect();

    if(viewrect==QRect() || viewrect!=currentviewrect) {
        if(viewrect==QRectF())
            viewrect = currentviewrect;

        if(viewrect.top()<=m_scene->sceneRect().top())
            viewrect.setTop(m_scene->sceneRect().top()-0.1);
        if(viewrect.bottom()>=m_scene->sceneRect().bottom())
            viewrect.setBottom(m_scene->sceneRect().bottom()+0.1);
        if(viewrect.left()<m_scene->sceneRect().left())
            viewrect.setLeft(m_scene->sceneRect().left());
        if(viewrect.right()>m_scene->sceneRect().right())
            viewrect.setRight(m_scene->sceneRect().right());

        fitInView(removeHiddenMargin(viewrect));

        if(sync){
            if(gMW->m_gvAmplitudeSpectrum && gMW->ui->actionShowAmplitudeSpectrum->isChecked()) {
                QRectF amprect = gMW->m_gvAmplitudeSpectrum->mapToScene(gMW->m_gvAmplitudeSpectrum->viewport()->rect()).boundingRect();
                amprect.setLeft(viewrect.left());
                amprect.setRight(viewrect.right());
                gMW->m_gvAmplitudeSpectrum->viewSet(amprect, false);

        //        QPointF p = gMW->m_gvAmplitudeSpectrum->mapToScene(gMW->m_gvAmplitudeSpectrum->viewport()->rect()).boundingRect().center();
        //        p.setX(viewrect.center().x());
        //        gMW->m_gvAmplitudeSpectrum->centerOn(p);
            }
        }
    }
//    cout << "QGVPhaseSpectrum::~viewSet" << endl;
}

void QGVPhaseSpectrum::resizeEvent(QResizeEvent* event) {
//    COUTD << "QGVPhaseSpectrum::resizeEvent" << endl;

    QRectF oldviewrect = mapToScene(QRect(QPoint(0,0), event->oldSize())).boundingRect();

    if(event->oldSize().isEmpty() && !event->size().isEmpty()) {

        updateSceneRect();

        if(gMW->m_gvAmplitudeSpectrum->viewport()->rect().width()*gMW->m_gvAmplitudeSpectrum->viewport()->rect().height()>0){
            QRectF phaserect = gMW->m_gvAmplitudeSpectrum->mapToScene(gMW->m_gvAmplitudeSpectrum->viewport()->rect()).boundingRect();

            QRectF viewrect;
            viewrect.setLeft(phaserect.left());
            viewrect.setRight(phaserect.right());
            viewrect.setTop(-M_PI);
            viewrect.setBottom(+M_PI);
            viewSet(viewrect, false);
        }
        else
            viewSet(m_scene->sceneRect(), false);
    }
    else if(!event->oldSize().isEmpty() && !event->size().isEmpty())
    {
        viewSet(oldviewrect, false);
    }

//    // Set the scroll bars in the amplitude and phase views
//    if(event->size().height()==0)
//        gMW->m_gvAmplitudeSpectrum->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
//    else
//        gMW->m_gvAmplitudeSpectrum->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    viewUpdateTexts();
    setMouseCursorPosition(QPointF(-1,0), false);

//    COUTD << "QGVPhaseSpectrum::~resizeEvent" << endl;
}

void QGVPhaseSpectrum::scrollContentsBy(int dx, int dy){

    // Invalidate the necessary parts
    // Ensure the y ticks labels will be redrawn
    QRectF viewrect = mapToScene(viewport()->rect()).boundingRect();
    QTransform trans = transform();

    QRectF r = QRectF(viewrect.left(), viewrect.top(), 5*14.0/trans.m11(), viewrect.height());
    m_scene->invalidate(r);

    r = QRectF(viewrect.left(), viewrect.bottom()-14/trans.m22(), viewrect.width(), 14/trans.m22());
    m_scene->invalidate(r);

    viewUpdateTexts();
    setMouseCursorPosition(QPointF(-1,0), false);

    QGraphicsView::scrollContentsBy(dx, dy);
}

void QGVPhaseSpectrum::wheelEvent(QWheelEvent* event) {
//    cout << "QGVPhaseSpectrum::wheelEvent" << endl;

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

//    cout << "QGVPhaseSpectrum::~wheelEvent" << endl;
}

void QGVPhaseSpectrum::mousePressEvent(QMouseEvent* event){
//    std::cout << "QGVWaveform::mousePressEvent" << endl;

    QPointF p = mapToScene(event->pos());
    QRect selview = mapFromScene(m_selection).boundingRect();

    bool kshift = event->modifiers().testFlag(Qt::ShiftModifier);
    bool kctrl = event->modifiers().testFlag(Qt::ControlModifier);

    if(event->buttons()&Qt::LeftButton){
        if(gMW->ui->actionSelectionMode->isChecked()){
            if(kshift && !kctrl) {
                // When moving the spectrum's view
                m_currentAction = CAMoving;
                setDragMode(QGraphicsView::ScrollHandDrag);
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
        else if(gMW->ui->actionEditMode->isChecked()){
            if(kctrl && kshift) {
                m_currentAction = CAWaveformDelay;
                m_selection_pressedp = p;
                FTSound* currentftsound = gMW->getCurrentFTSound();
                if(currentftsound)
                    m_pressed_delay = currentftsound->m_delay;
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
        else {
            QPoint posglobal = mapToGlobal(mapFromScene(p)+QPoint(0,0));
            m_contextmenu.exec(posglobal);
        }
    }

    QGraphicsView::mousePressEvent(event);
//    std::cout << "~QGVWaveform::mousePressEvent " << p.x() << endl;
}

void QGVPhaseSpectrum::mouseMoveEvent(QMouseEvent* event){
//    std::cout << "QGVWaveform::mouseMoveEvent" << selection.width() << endl;

    QPointF p = mapToScene(event->pos());

//    std::cout << "QGVWaveform::mouseMoveEvent action=" << m_currentAction << " x=" << p.x() << " y=" << p.y() << endl;

    if(m_currentAction==CAMoving) {
        // When scrolling the view around the scene
//        setMouseCursorPosition(QPointF(-1,0), true);
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
        double dy = -(p.y()-m_selection_pressedp.y());
        // cout << "CAMovingWaveformSelection at " << m_selection_pressedp.x() << " " << dy << "rad" << endl;
        double dt = ((gMW->getFs()/m_selection_pressedp.x())*dy/(2*M_PI))/gMW->getFs();
        QRectF wavsel = m_wavselection_pressed;
        wavsel.setLeft(wavsel.left()+dt);
        wavsel.setRight(wavsel.right()+dt);
        gMW->m_gvWaveform->selectionSet(wavsel);
    }
    else if(m_currentAction==CAWaveformDelay) {
        double dy = -(p.y()-m_selection_pressedp.y());
        // cout << "CAWaveformDelay at " << m_selection_pressedp.x() << " " << dy << "rad" << endl;
        double dt = ((gMW->getFs()/m_selection_pressedp.x())*dy/(2*M_PI))/gMW->getFs();
        FTSound* currentftsound = gMW->getCurrentFTSound();
        if(currentftsound){
            currentftsound->m_delay = m_pressed_delay - dt*gMW->getFs();

            currentftsound->needDFTUpdate();

            gMW->m_gvWaveform->m_scene->update();
            gMW->m_gvAmplitudeSpectrum->allSoundsChanged();
            gMW->fileInfoUpdate();
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

    setMouseCursorPosition(p, true);

//    std::cout << "~QGVWaveform::mouseMoveEvent" << endl;
    QGraphicsView::mouseMoveEvent(event);
}

void QGVPhaseSpectrum::mouseReleaseEvent(QMouseEvent* event){
//    std::cout << "QGVWaveform::mouseReleaseEvent " << selection.width() << endl;

    QPointF p = mapToScene(event->pos());

    m_currentAction = CANothing;

    if(gMW->ui->actionSelectionMode->isChecked()){
        if(event->modifiers().testFlag(Qt::ShiftModifier)){
            setCursor(Qt::OpenHandCursor);
        }
        else if(event->modifiers().testFlag(Qt::ControlModifier)){
            setCursor(Qt::OpenHandCursor);
        }
        else{
            if(p.x()>=m_selection.left() && p.x()<=m_selection.right() && p.y()>=m_selection.top() && p.y()<=m_selection.bottom())
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
            setCursor(Qt::SizeVerCursor);
        }
    }

    if(abs(m_selection.width())<=0 || abs(m_selection.height())<=0)
        gMW->m_gvAmplitudeSpectrum->selectionClear();

    QGraphicsView::mouseReleaseEvent(event);
//    std::cout << "~QGVWaveform::mouseReleaseEvent " << endl;
}

void QGVPhaseSpectrum::keyPressEvent(QKeyEvent* event){
    if(event->key()==Qt::Key_Escape)
        gMW->m_gvAmplitudeSpectrum->selectionClear();

    QGraphicsView::keyPressEvent(event);
}

void QGVPhaseSpectrum::selectionClear(bool forwardsync){
    Q_UNUSED(forwardsync)
    m_selection = QRectF(0, 0, 0, 0);
    m_mouseSelection = QRectF(0, 0, 0, 0);
    m_giShownSelection->hide();
    m_giShownSelection->setRect(QRectF(0, 0, 0, 0));
    m_giSelectionTxt->hide();
    setCursor(Qt::CrossCursor);
}

void QGVPhaseSpectrum::selectionSet(QRectF selection, bool forwardsync){
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
    if(m_selection.right()>gMW->getFs()/2.0) m_selection.setRight(gMW->getFs()/2.0);
    if(m_selection.top()<m_scene->sceneRect().top()) m_selection.setTop(m_scene->sceneRect().top());
    if(m_selection.bottom()>m_scene->sceneRect().bottom()) m_selection.setBottom(m_scene->sceneRect().bottom());

    m_giShownSelection->setRect(m_selection);
    m_giShownSelection->show();

    m_giSelectionTxt->setText(QString("%1Hz,%2rad").arg(m_selection.width()).arg(m_selection.height()));
//    m_giSelectionTxt->show();
    viewUpdateTexts();

    if(gMW->m_gvAmplitudeSpectrum && forwardsync){
        QRectF rect = gMW->m_gvAmplitudeSpectrum->m_mouseSelection;
        rect.setLeft(m_mouseSelection.left());
        rect.setRight(m_mouseSelection.right());
        if(rect.height()==0) {
            rect.setTop(gMW->m_gvAmplitudeSpectrum->m_scene->sceneRect().top());
            rect.setBottom(gMW->m_gvAmplitudeSpectrum->m_scene->sceneRect().bottom());
        }
        gMW->m_gvAmplitudeSpectrum->selectionSet(rect, false);
    }
}

void QGVPhaseSpectrum::viewUpdateTexts(){
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

void QGVPhaseSpectrum::azoomin(){
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
void QGVPhaseSpectrum::azoomout(){
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

void QGVPhaseSpectrum::setMouseCursorPosition(QPointF p, bool forwardsync) {

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
        m_giCursorHoriz->setLine(viewrect.right()-50/trans.m11(), m_giCursorHoriz->line().y1(), gMW->getFs()/2.0, m_giCursorHoriz->line().y1());

        m_giCursorVert->show();
        m_giCursorVert->setLine(m_giCursorVert->line().x1(), viewrect.top(), m_giCursorVert->line().x1(), viewrect.top()+18/trans.m22());

        m_giCursorPositionXTxt->show();
        m_giCursorPositionXTxt->setTransform(txttrans);
        m_giCursorPositionXTxt->setText(QString("%1Hz").arg(m_giCursorVert->line().x1()));
        QRectF br = m_giCursorPositionXTxt->boundingRect();
        qreal x = m_giCursorVert->line().x1()+1/trans.m11();
        x = min(x, viewrect.right()-br.width()/trans.m11());
        m_giCursorPositionXTxt->setPos(x, viewrect.top());

        m_giCursorPositionYTxt->show();
        m_giCursorPositionYTxt->setTransform(txttrans);
        m_giCursorPositionYTxt->setText(QString("%1rad").arg(-m_giCursorHoriz->line().y1()));
        br = m_giCursorPositionYTxt->boundingRect();
        m_giCursorPositionYTxt->setPos(viewrect.right()-br.width()/trans.m11(), m_giCursorHoriz->line().y1()-br.height()/trans.m22());
    }

    if(forwardsync){
        if(gMW->m_gvSpectrogram)
            gMW->m_gvSpectrogram->setMouseCursorPosition(QPointF(0.0, p.x()), false);
        if(gMW->m_gvAmplitudeSpectrum)
            gMW->m_gvAmplitudeSpectrum->setMouseCursorPosition(QPointF(p.x(), 0.0), false);
    }
}

void QGVPhaseSpectrum::drawBackground(QPainter* painter, const QRectF& rect){

//    COUTD << ": QGVPhaseSpectrum::drawBackground " << rect.left() << " " << rect.right() << " " << rect.top() << " " << rect.bottom() << endl;

    // QGraphicsView::drawBackground(painter, rect);// TODO Need this ??

    // Draw grid
    if(gMW->m_gvAmplitudeSpectrum->m_aAmplitudeSpectrumShowGrid->isChecked())
        draw_grid(painter, rect);

    // Draw the f0 grids
    if(!gMW->ftfzeros.empty()) {

        for(size_t fi=0; fi<gMW->ftfzeros.size(); fi++){
            if(!gMW->ftfzeros[fi]->m_actionShow->isChecked())
                continue;

            QPen outlinePen(gMW->ftfzeros[fi]->color);
            outlinePen.setWidth(0);
            painter->setPen(outlinePen);
            painter->setBrush(QBrush(gMW->ftfzeros[fi]->color));

            double ct = 0.5*(gMW->m_gvAmplitudeSpectrum->m_nl+gMW->m_gvAmplitudeSpectrum->m_nr)/gMW->getFs();
            double cf0 = sigproc::nearest<double>(gMW->ftfzeros[fi]->ts, gMW->ftfzeros[fi]->f0s, ct, -1.0);

            // cout << ct << ":" << cf0 << endl;
            if(cf0==-1) continue;

            QColor c = gMW->ftfzeros[fi]->color;
            c.setAlphaF(1.0);
            outlinePen.setColor(c);
            painter->setPen(outlinePen);
            painter->drawLine(QLineF(cf0, -M_PI, cf0, M_PI));

            c.setAlphaF(0.5);
            outlinePen.setColor(c);
            painter->setPen(outlinePen);

            for(int h=2; h<int(0.5*gMW->getFs()/cf0)+1; h++)
                painter->drawLine(QLineF(h*cf0, -M_PI, h*cf0, M_PI));
        }
    }

    if (gMW->m_gvWaveform->m_selection.width()==0)
        return;

    // Draw the phase spectra

    FTSound* currsnd = gMW->getCurrentFTSound(true);

    for(size_t fi=0; fi<gMW->ftsnds.size(); fi++){
        if(!gMW->m_gvWaveform->m_aWaveformShowSelectedWaveformOnTop->isChecked() || gMW->ftsnds[fi]!=currsnd){
            if(gMW->ftsnds[fi]->m_actionShow->isChecked()){
                QPen outlinePen(gMW->ftsnds[fi]->color);
                outlinePen.setWidth(0);
                painter->setPen(outlinePen);
                painter->setBrush(QBrush(gMW->ftsnds[fi]->color));

                draw_spectrum(painter, gMW->ftsnds[fi]->m_dft, gMW->getFs(), (gMW->m_gvAmplitudeSpectrum->m_winlen-1)/2.0, rect);
            }
        }
    }

    if(gMW->m_gvWaveform->m_aWaveformShowSelectedWaveformOnTop->isChecked()){
        if(currsnd->m_actionShow->isChecked()){
            QPen outlinePen(currsnd->color);
            outlinePen.setWidth(0);
            painter->setPen(outlinePen);
            painter->setBrush(QBrush(currsnd->color));

            draw_spectrum(painter, currsnd->m_dft, gMW->getFs(), (gMW->m_gvAmplitudeSpectrum->m_winlen-1)/2.0, rect);
        }
    }

//    cout << "QGVPhaseSpectrum::~drawBackground" << endl;
}

void QGVPhaseSpectrum::draw_spectrum(QPainter* painter, std::vector<std::complex<WAVTYPE> >& ldft, double fs, double delay, const QRectF& rect) {
    int dftlen = (int(ldft.size())-1)*2;
    if (dftlen<2)
        return;

    QRectF viewrect = mapToScene(viewport()->rect()).boundingRect();

    int kmin = std::max(0, int(dftlen*rect.left()/fs));
    int kmax = std::min(dftlen/2, int(1+dftlen*rect.right()/fs));

    // Draw the sound's spectra
    double samppixdensity = (dftlen*(viewrect.right()-viewrect.left())/fs)/viewport()->rect().width();

    if(samppixdensity<=1.0) {
//         cout << "Spec: Draw lines between each bin" << endl;

        double prevx = fs*kmin/dftlen;
        double dp = delay*2.0*M_PI*kmin/dftlen;
        dp += ldft[kmin].imag();
        double prevy = std::arg(std::complex<WAVTYPE>(cos(dp),sin(dp)));
        std::complex<WAVTYPE>* data = ldft.data();
        for(int k=kmin+1; k<=kmax; ++k){
            double x = fs*k/dftlen;
            dp = delay*2.0*M_PI*k/dftlen;
            dp += (*(data+k)).imag();
            double y = std::arg(std::complex<WAVTYPE>(cos(dp),sin(dp))); // TODO Use wrap instead
            painter->drawLine(QLineF(prevx, -prevy, x, -y));
            prevx = x;
            prevy = y;
        }
    }
    else {
//         cout << "Spec: Plot only one line per pixel, in order to reduce computation time" << endl;

        painter->setWorldMatrixEnabled(false); // Work in pixel coordinates

        QRect pixrect = mapFromScene(rect).boundingRect();
        QRect fullpixrect = mapFromScene(viewrect).boundingRect();

        double s2p = -fullpixrect.height()/viewrect.height(); // Scene to pixel
        double p2s = viewrect.width()/fullpixrect.width(); // Pixel to scene
        double yzero = mapFromScene(QPointF(0,0)).y();

        std::complex<WAVTYPE>* yp = ldft.data();

        for(int i=pixrect.left(); i<=pixrect.right(); i++) {
            int ns = int(dftlen*(viewrect.left()+i*p2s)/fs);
            int ne = int(dftlen*(viewrect.left()+(i+1)*p2s)/fs);

            if(ns>=0 && ne<int(ldft.size())) {
                WAVTYPE ymin = std::numeric_limits<double>::infinity();
                WAVTYPE ymax = -std::numeric_limits<double>::infinity();
                std::complex<WAVTYPE>* ypp = yp+ns;
                WAVTYPE y;
                for(int n=ns; n<=ne; n++) {
                    double dp = delay*2.0*M_PI*n/dftlen;
                    dp += (*ypp).imag();
                    y = std::arg(std::complex<WAVTYPE>(cos(dp),sin(dp))); // TODO Use wrap instead
                    ymin = std::min(ymin, y);
                    ymax = std::max(ymax, y);
                    ypp++;
                }
                ymin *= s2p;
                ymax *= s2p;
                ymin = int(ymin+0.5);
                ymax = int(ymax+0.5);
                painter->drawLine(QLineF(i, yzero+ymin, i, yzero+ymax));
            }
        }

        painter->setWorldMatrixEnabled(true); // Go back to scene coordinates
    }
}

void QGVPhaseSpectrum::draw_grid(QPainter* painter, const QRectF& rect){

    // TODO Everything here could be save a lot of drawing instructions

    // Prepare the pens and fonts
    QPen gridPen(QColor(192,192,192)); //192
    gridPen.setWidth(0); // Cosmetic pen (width=1pixel whatever the transform)
    QPen thinGridPen(QColor(224,224,224));
    thinGridPen.setWidth(0); // Cosmetic pen (width=1pixel whatever the transform)
    QPen gridFontPen(QColor(128,128,128));
    gridFontPen.setWidth(0); // Cosmetic pen (width=1pixel whatever the transform)
    QFont font;
    font.setPixelSize(12);
    painter->setFont(font);

    // Horizontal lines

    QRectF viewrect = mapToScene(viewport()->rect()).boundingRect();

    // Adapt the lines ordinates to the viewport
    double f = log10(float(viewrect.height()));
    int fi;
    if(f<0) fi=int(f-1);
    else fi = int(f);
    double lstep = pow(10.0, fi);
    int m=1;
    while(int(viewrect.height()/lstep)<3){
        lstep /= 2;
        m++;
    }

    // Draw the horizontal lines
    int mn=0;
    painter->setPen(gridPen);
    painter->drawLine(QLineF(rect.left(), 0, rect.right(), 0));
//    for(double l=int(viewrect.top()/lstep)*lstep; l<=rect.bottom(); l+=lstep){
    for(double l=0.0; l<=rect.bottom(); l+=lstep){
//        if(mn%m==0) painter->setPen(gridPen);
//        else        painter->setPen(thinGridPen);
        painter->drawLine(QLineF(rect.left(), l, rect.right(), l));
        mn++;
    }
    for(double l=0.0; l>=rect.top(); l-=lstep){
//        if(mn%m==0) painter->setPen(gridPen);
//        else        painter->setPen(thinGridPen);
        painter->drawLine(QLineF(rect.left(), l, rect.right(), l));
        mn++;
    }

    // Write the ordinates of the horizontal lines
    painter->setPen(gridFontPen);
    QTransform trans = transform();
    for(double l=0.0; l<=rect.bottom(); l+=lstep) {
        painter->save();
        painter->translate(QPointF(viewrect.left(), l));
        painter->scale(1.0/trans.m11(), 1.0/trans.m22());

        QString txt = QString("%1rad").arg(-l);
        QRectF txtrect = painter->boundingRect(QRectF(), Qt::AlignLeft, txt);
        if(l<viewrect.bottom()-0.75*txtrect.height()/trans.m22())
            painter->drawStaticText(QPointF(0, -0.9*txtrect.height()), QStaticText(txt));
        painter->restore();
    }
    for(double l=0.0; l>=rect.top(); l-=lstep) {
        painter->save();
        painter->translate(QPointF(viewrect.left(), l));
        painter->scale(1.0/trans.m11(), 1.0/trans.m22());

        QString txt = QString("%1rad").arg(-l);
        QRectF txtrect = painter->boundingRect(QRectF(), Qt::AlignLeft, txt);
        if(l<viewrect.bottom()-0.75*txtrect.height()/trans.m22())
            painter->drawStaticText(QPointF(0, -0.9*txtrect.height()), QStaticText(txt));
        painter->restore();
    }

    // Vertical lines

    // Adapt the lines absissa to the viewport
    f = log10(float(viewrect.width()));
    if(f<0) fi=int(f-1);
    else fi = int(f);
//    std::cout << viewrect.height() << " " << f << " " << fi << endl;
    lstep = pow(10.0, fi);
    m=1;
    while(int(viewrect.width()/lstep)<6) {
        lstep /= 2;
        m++;
    }
//    std::cout << "lstep=" << lstep << endl;

    // Draw the vertical lines
    mn = 0;
    painter->setPen(gridPen);
    for(double l=int(viewrect.left()/lstep)*lstep; l<=rect.right(); l+=lstep){
//        if(mn%m==0) painter->setPen(gridPen);
//        else        painter->setPen(thinGridPen);
        painter->drawLine(QLineF(l, rect.top(), l, rect.bottom()));
        mn++;
    }

    // Write the absissa of the vertical lines
    painter->setPen(gridFontPen);
    trans = transform();
    for(double l=int(viewrect.left()/lstep)*lstep; l<=rect.right(); l+=lstep) {
        painter->save();
        painter->translate(QPointF(l, viewrect.bottom()-14.0/trans.m22()));
        painter->scale(1.0/trans.m11(), 1.0/trans.m22());
        painter->drawStaticText(QPointF(0, 0), QStaticText(QString("%1Hz").arg(l)));
        painter->restore();
    }
}

QGVPhaseSpectrum::~QGVPhaseSpectrum() {
}
