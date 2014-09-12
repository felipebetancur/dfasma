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

#include "ftsound.h"

#include <iostream>
#include <limits>
#include <deque>
using namespace std;

#include <qmath.h>
#include <qendian.h>
#include <QMenu>
#include <QMessageBox>
#include "wmainwindow.h"
#include "ui_wmainwindow.h"
#include "gvamplitudespectrum.h"
#include "gvwaveform.h"
#include "sigproc.h"

#include "../external/mkfilter/mkfilter.h"

#include "ui_wdialogsettings.h"

bool FTSound::sm_playwin_use = false;
std::vector<WAVTYPE> FTSound::sm_avoidclickswindow;

double FTSound::fs_common = 0; // Initially, fs is undefined TODO put in wmainwindow
WAVTYPE FTSound::s_play_power = 0;
std::deque<WAVTYPE> FTSound::s_play_power_values;

FTSound::FTSound(const QString& _fileName, QObject *parent)
    : QIODevice(parent)
    , FileType(FTSOUND, _fileName, this)
    , wavtoplay(&wav)
    , m_ampscale(1.0)
    , m_delay(0)
    , m_start(0)
    , m_pos(0)
    , m_end(0)
    , m_avoidclickswinpos(0)
{
    m_actionInvPolarity = new QAction("Inverse polarity", this);
    m_actionInvPolarity->setStatusTip(tr("Inverse the polarity of the sound"));
    m_actionInvPolarity->setCheckable(true);
    m_actionInvPolarity->setChecked(false);

    m_actionResetAmpScale = new QAction("Reset amplitude", this);
    m_actionResetAmpScale->setStatusTip(tr("Reset the amplitude scaling to 1"));

    m_actionResetDelay = new QAction("Reset the delay", this);
    m_actionResetDelay->setStatusTip(tr("Reset the delay to 0s"));

    connect(m_actionReload, SIGNAL(triggered()), this, SLOT(reload()));

    load(_fileName);
    load_finalize();

//    QIODevice::open(QIODevice::ReadOnly);
}

void FTSound::load_finalize() {
    m_wavmaxamp = 0.0;
    for(unsigned int n=0; n<wav.size(); ++n)
        m_wavmaxamp = std::max(m_wavmaxamp, abs(wav[n]));

    if(sm_avoidclickswindow.size()==0)
        FTSound::setAvoidClicksWindowDuration(WMainWindow::getMW()->m_dlgSettings->ui->sbAvoidClicksWindowDuration->value());

    std::cout << "INFO: " << wav.size() << " samples loaded (" << wav.size()/fs << "s max amplitude=" << m_wavmaxamp << ")" << endl;
}

void FTSound::reload() {
//    cout << "FTSound::reload" << endl;

    stop();

    // Reset everything ...
    wavtoplay = &wav;
    m_ampscale = 1.0;
    m_delay = 0;
    m_start = 0;
    m_pos = 0;
    m_end = 0;
    m_avoidclickswinpos = 0;
    wav.clear();
    wavfiltered.clear();

    // ... and reload the data from the file
    load(fileFullPath);
    load_finalize();

    setTexts();
}

QString FTSound::info() const {
    QString str = "";
    QString codecname = m_fileaudioformat.codec();
//    if(codecname.isEmpty()) codecname = "unknown type";
//    str += "Codec: "+codecname+"<br/>";
//    if(m_fileaudioformat.channelCount()!=-1)
//        str += "Channels: "+QString::number(m_fileaudioformat.channelCount())+" channel<br/>";
    str += "Sampling frequency: "+QString::number(m_fileaudioformat.sampleRate())+"Hz<br/>";
    if(m_fileaudioformat.sampleSize()!=-1) {
        str += "Sample type: "+QString::number(m_fileaudioformat.sampleSize())+"b ";
        QAudioFormat::SampleType sampletype = m_fileaudioformat.sampleType();
        if(sampletype==QAudioFormat::Unknown)
            str += "(unknown type)";
        else if(sampletype==QAudioFormat::SignedInt)
            str += "signed integer";
        else if(sampletype==QAudioFormat::UnSignedInt)
            str += "unsigned interger";
        else if(sampletype==QAudioFormat::Float)
            str += "float";
        str += "<br/>";
        str += "SQNR="+QString::number(20*std::log10(std::pow(2,m_fileaudioformat.sampleSize())))+"dB<br/>";
        if(sampletype!=QAudioFormat::Unknown) {
            double smallest=1.0;
            if(sampletype==QAudioFormat::SignedInt)
                smallest = 1.0/std::pow(2.0,m_fileaudioformat.sampleSize()-1);
            else if(sampletype==QAudioFormat::UnSignedInt)
                smallest = 2.0/std::pow(2.0,m_fileaudioformat.sampleSize());
            else if(sampletype==QAudioFormat::Float) {
                if(m_fileaudioformat.sampleSize()==8*sizeof(float))
                    smallest = std::numeric_limits<float>::min();
                else if(m_fileaudioformat.sampleSize()==8*sizeof(double))
                    smallest = std::numeric_limits<double>::min();
                else if(m_fileaudioformat.sampleSize()==8*sizeof(long double))
                    smallest = std::numeric_limits<long double>::min();
            }
            if(smallest!=1.0)
                str += "Smallest amplitude: "+QString::number(20*log10(smallest))+"dB";
        }
    }

    return str;
}

void FTSound::setAvoidClicksWindowDuration(double halfduration) {
    sm_avoidclickswindow = sigproc::hann(2*int(2*halfduration*fs_common/2)+1); // Use 50ms half-windows on each side
    double winmax = sm_avoidclickswindow[(sm_avoidclickswindow.size()-1)/2];
    for(size_t n=0; n<sm_avoidclickswindow.size(); n++)
        sm_avoidclickswindow[n] /= winmax;
}

void FTSound::fillContextMenu(QMenu& contextmenu, WMainWindow* mainwindow) {

    FileType::fillContextMenu(contextmenu, mainwindow);

    connect(m_actionReload, SIGNAL(triggered()), mainwindow, SLOT(soundsChanged()));

    contextmenu.setTitle("Sound");

    contextmenu.addAction(mainwindow->ui->actionPlay);
    contextmenu.addAction(m_actionInvPolarity);
    connect(m_actionInvPolarity, SIGNAL(toggled(bool)), mainwindow, SLOT(soundsChanged()));
    m_actionResetAmpScale->setText(QString("Reset amplitude scaling (%1dB) to 0dB").arg(20*log10(m_ampscale), 0, 'g', 3));
    m_actionResetAmpScale->setDisabled(m_ampscale==1.0);
    contextmenu.addAction(m_actionResetAmpScale);
    connect(m_actionResetAmpScale, SIGNAL(triggered()), mainwindow, SLOT(resetAmpScale()));
    m_actionResetDelay->setText(QString("Reset delay (%1s) to 0s").arg(m_delay/mainwindow->getFs(), 0, 'g', 3));
    m_actionResetDelay->setDisabled(m_delay==0);
    contextmenu.addAction(m_actionResetDelay);
    connect(m_actionResetDelay, SIGNAL(triggered()), mainwindow, SLOT(resetDelay()));
}

double FTSound::getLastSampleTime() const {
    return (wav.size()-1)/fs;
}
bool FTSound::isModified() {
    return m_delay!=0.0 || m_ampscale!=1.0;
}

void FTSound::setSamplingRate(double _fs){

    fs = _fs;

    // Check if fs is the same for all files
    if(fs_common==0) {
        // The system has no defined sampling rate
        fs_common = fs;
        FTSound::setAvoidClicksWindowDuration(WMainWindow::getMW()->m_dlgSettings->ui->sbAvoidClicksWindowDuration->value());
    }
    else {
        // Check if fs is the same as that of the other files
        if(fs_common!=fs)
            throw QString("The sampling rate is not the same as that of the files already loaded.");
    }
}

double FTSound::setPlay(const QAudioFormat& format, double tstart, double tstop, double fstart, double fstop)
{
//    cout << "FTSound::setPlay" << endl;

    // Start by reseting any other filtered sounds
    for(size_t fi=0; fi<WMainWindow::getMW()->ftsnds.size(); fi++)
        if(WMainWindow::getMW()->ftsnds[fi]!=this)
            WMainWindow::getMW()->ftsnds[fi]->wavtoplay = &(WMainWindow::getMW()->ftsnds[fi]->wav);

    m_outputaudioformat = format;

    s_play_power = 0;
    s_play_power_values.clear();
    m_avoidclickswinpos = 0;

    // Fix and make time selection
    if(tstart>tstop){
        double tmp = tstop;
        tstop = tstart;
        tstart = tmp;
    }

    if(tstart==0.0 && tstop==0.0){
        m_start = 0;
        m_pos = m_start;
        m_end = wav.size()-1;
    }
    else{
        m_start = int(0.5+tstart*fs);
        m_pos = m_start;
        m_end = int(0.5+tstop*fs);
    }

    if(m_start<0) m_start=0;
    if(m_start>wav.size()-1) m_start=wav.size()-1;

    if(m_end<0) m_end=0;
    if(m_end>wav.size()-1) m_end=wav.size()-1;


    // Fix frequency cutoffs
    if(fstart>fstop){
        double tmp = fstop;
        fstop = fstart;
        fstart = tmp;
    }

    // Cannot ensure the numerical stability for very small cutoff
    // Thus, clip the given values
    if(fstart<10) fstart=0;
    if(fstart>fs/2-10) fstart=fs/2;
    if(fstop<10) fstop=0;
    if(fstop>fs/2-10) fstop=fs/2;

    bool doLowPass = fstop>0.0 && fstop<fs/2;
    bool doHighPass = fstart>0.0 && fstart<fs/2;

    if ((fstart<fstop) && (doLowPass || doHighPass)) {
        try{
            wavfiltered = wav;

            int butterworth_order = WMainWindow::getMW()->m_dlgSettings->ui->sbButterworthOrder->value();
            WMainWindow::getMW()->m_gvSpectrum->m_filterresponse = std::vector<FFTTYPE>(BUTTERRESPONSEDFTLEN/2+1,1.0);
            std::vector< std::vector<double> > num, den;
            std::vector<double> filterresponse;

            if (doLowPass) {
                mkfilter::make_butterworth_filter_biquad(butterworth_order, fstop/fs, true, num, den, &filterresponse, BUTTERRESPONSEDFTLEN);

                for(size_t k=0; k<filterresponse.size(); k++){
                    if(filterresponse[k] < 2*std::numeric_limits<FFTTYPE>::min())
                        filterresponse[k] = std::numeric_limits<FFTTYPE>::min();
                    WMainWindow::getMW()->m_gvSpectrum->m_filterresponse[k] *= filterresponse[k];
                }

                WMainWindow::getMW()->m_globalWaitingBarLabel->setText(QString("Low-pass filtering (cutoff=")+QString::number(fstop)+"Hz)");
                WMainWindow::getMW()->m_globalWaitingBarLabel->show();
                WMainWindow::getMW()->m_globalWaitingBar->setMinimum(0);
                WMainWindow::getMW()->m_globalWaitingBar->setMaximum(0);
                WMainWindow::getMW()->m_globalWaitingBar->show();
                WMainWindow::getMW()->ui->statusBar->repaint();

                cout << "LP-filtering (cutoff=" << fstop << ", size=" << wavfiltered.size() << ")" << endl;
                for(size_t bi=0; bi<num.size(); bi++)
                    sigproc::filtfilt<WAVTYPE>(wavfiltered, num[bi], den[bi], wavfiltered, m_start, m_end);

                WMainWindow::getMW()->m_globalWaitingBarLabel->hide();
                WMainWindow::getMW()->m_globalWaitingBar->hide();
            }

            if (doHighPass) {
                mkfilter::make_butterworth_filter_biquad(butterworth_order, fstart/fs, false, num, den, &filterresponse, BUTTERRESPONSEDFTLEN);

                for(size_t k=0; k<filterresponse.size(); k++){
                    if(filterresponse[k] < 2*std::numeric_limits<FFTTYPE>::min())
                        filterresponse[k] = std::numeric_limits<FFTTYPE>::min();
                    WMainWindow::getMW()->m_gvSpectrum->m_filterresponse[k] *= filterresponse[k];
                }

                WMainWindow::getMW()->m_globalWaitingBarLabel->setText(QString("High-pass filtering (cutoff=")+QString::number(fstart)+"Hz)");
                WMainWindow::getMW()->m_globalWaitingBarLabel->show();
                WMainWindow::getMW()->m_globalWaitingBar->setMinimum(0);
                WMainWindow::getMW()->m_globalWaitingBar->setMaximum(0);
                WMainWindow::getMW()->m_globalWaitingBar->show();
                WMainWindow::getMW()->ui->statusBar->repaint();

                cout << "HP-filtering (cutoff=" << fstart << ", size=" << wavfiltered.size() << ")" << endl;

                for(size_t bi=0; bi<num.size(); bi++)
                    sigproc::filtfilt<WAVTYPE>(wavfiltered, num[bi], den[bi], wavfiltered, m_start, m_end);

                WMainWindow::getMW()->m_globalWaitingBarLabel->hide();
                WMainWindow::getMW()->m_globalWaitingBar->hide();
            }

            // Convert to dB and multiply by 2 bcs of the filtfilt
            for(size_t k=0; k<WMainWindow::getMW()->m_gvSpectrum->m_filterresponse.size(); k++)
                WMainWindow::getMW()->m_gvSpectrum->m_filterresponse[k] = 2*20*log10(WMainWindow::getMW()->m_gvSpectrum->m_filterresponse[k]);
            WMainWindow::getMW()->m_gvSpectrum->m_scene->invalidate();

            // It seems the filtering went well, we can use the filtered sound
            wavtoplay = &wavfiltered;

            WMainWindow::getMW()->m_gvWaveform->m_scene->invalidate();
            WMainWindow::getMW()->m_gvSpectrum->computeDFTs();
            WMainWindow::getMW()->m_gvSpectrum->m_scene->invalidate();
        }
        catch(QString err){
            QMessageBox::warning(NULL, "Problem when filtering the sound to be played", QString("The sound cannot be filtered as given by the selection in the spectrum view.\n\nReason:\n")+err);
//            wavtoplay = &wav;
            throw QString("Problem when filtering the sound to be played");
        }
    }
    else {
        WMainWindow::getMW()->m_gvSpectrum->m_filterresponse.resize(0);
        WMainWindow::getMW()->m_gvSpectrum->m_scene->invalidate();
        bool dofrefresh = wavtoplay != &wav;
        wavtoplay = &wav;
        if (dofrefresh) {
            WMainWindow::getMW()->m_gvWaveform->m_scene->invalidate();
            WMainWindow::getMW()->m_gvSpectrum->computeDFTs();
            WMainWindow::getMW()->m_gvSpectrum->m_scene->invalidate();
        }
    }

    QIODevice::open(QIODevice::ReadOnly);

    double tobeplayed = double(m_end-m_pos+1)/fs;

//    std::cout << "DSSound::start [" << tstart << "s(" << m_pos << "), " << tstop << "s(" << m_end << ")] " << tobeplayed << "s" << endl;
//    cout << "FTSound::~setPlay" << endl;

    return tobeplayed;
}

void FTSound::stop()
{
    m_start = 0;
    m_pos = 0;
    m_end = 0;
    m_avoidclickswinpos = 0;
    QIODevice::close();
}

qint64 FTSound::readData(char *data, qint64 askedlen)
{
//    std::cout << "DSSound::readData requested=" << askedlen << endl;

    qint64 writtenbytes = 0; // [bytes]

    const int channelBytes = m_outputaudioformat.sampleSize() / 8;

    unsigned char *ptr = reinterpret_cast<unsigned char *>(data);

    // Polarity apparently matters in very particular cases
    // so take it into account when playing.
    double gain = m_ampscale;
    if(m_actionInvPolarity->isChecked())
        gain *= -1;

    // Write as many bits has requested by the call
    while(writtenbytes<askedlen) {
        qint16 value = 0;

        if(sm_playwin_use && (m_avoidclickswinpos<(sm_avoidclickswindow.size()-1)/2)) {
            value = qint16((gain*(*wavtoplay)[m_start]*sm_avoidclickswindow[m_avoidclickswinpos++])*32767);
        }
        else if(sm_playwin_use && (m_pos>m_end) && m_avoidclickswinpos<sm_avoidclickswindow.size()-1) {
            value = qint16((gain*(*wavtoplay)[m_end]*sm_avoidclickswindow[1+m_avoidclickswinpos++])*32767);
        }
        else if (m_pos<=m_end) {
            int depos = m_pos - m_delay;
            if(depos>=0 && depos<int((*wavtoplay).size())){
        //        WAVTYPE e = samples[m_pos]*samples[m_pos];
        //        s_play_power += e;
                WAVTYPE e = abs(gain*(*wavtoplay)[depos]);
                s_play_power_values.push_front(e);
                while(s_play_power_values.size()/fs>0.1){
                    s_play_power -= s_play_power_values.back();
                    s_play_power_values.pop_back();
                }
            }

        //        cout << 20*log10(sqrt(s_play_power/s_play_power_values.size())) << endl;

            // Assuming the output audio device has been open in 16bits ...
            // TODO Manage more output formats
            if(depos>=0 && depos<int((*wavtoplay).size()) && m_pos<=m_end)
                value=qint16((gain*(*wavtoplay)[depos])*32767);

            m_pos++;
        }

        qToLittleEndian<qint16>(value, ptr);
        ptr += channelBytes;
        writtenbytes += channelBytes;
    }

    s_play_power = 0;
    for(unsigned int i=0; i<s_play_power_values.size(); i++)
        s_play_power = max(s_play_power, s_play_power_values[i]);

//    std::cout << "~DSSound::readData writtenbytes=" << writtenbytes << " m_pos=" << m_pos << " m_end=" << m_end << endl;

    if(m_pos>=m_end){
//        std::cout << "STOP!!!" << endl;
//        QIODevice::close(); // TODO do this instead ??
//        emit readChannelFinished();
        return writtenbytes;
    }
    else{
        return writtenbytes;
    }
}

qint64 FTSound::writeData(const char *data, qint64 askedlen){

    Q_UNUSED(data)
    Q_UNUSED(askedlen)

    throw QString("DSSound::writeData: There is no reason to call this function.");

    return 0;
}

FTSound::~FTSound(){
    QIODevice::close();
}


//qint64 DSSound::bytesAvailable() const
//{
//    std::cout << "DSSound::bytesAvailable " << QIODevice::bytesAvailable() << endl;

//    return QIODevice::bytesAvailable();
////    return m_buffer.size() + QIODevice::bytesAvailable();
//}

/*
void Generator::generateData(const QAudioFormat &format, qint64 durationUs, int sampleRate)
{
    const int channelBytes = format.sampleSize() / 8;
//    std::cout << channelBytes << endl;
    const int sampleBytes = format.channelCount() * channelBytes;

    qint64 length = (format.sampleRate() * format.channelCount() * (format.sampleSize() / 8))
                        * durationUs / 1000000;

    Q_ASSERT(length % sampleBytes == 0);
    Q_UNUSED(sampleBytes) // suppress warning in release builds

    m_buffer.resize(length);
    unsigned char *ptr = reinterpret_cast<unsigned char *>(m_buffer.data());
    int sampleIndex = 0;

    while (length) {
        const qreal x = qSin(2 * M_PI * sampleRate * qreal(sampleIndex % format.sampleRate()) / format.sampleRate());
        for (int i=0; i<format.channelCount(); ++i) {
            if (format.sampleSize() == 8 && format.sampleType() == QAudioFormat::UnSignedInt) {
                const quint8 value = static_cast<quint8>((1.0 + x) / 2 * 255);
                *reinterpret_cast<quint8*>(ptr) = value;
            } else if (format.sampleSize() == 8 && format.sampleType() == QAudioFormat::SignedInt) {
                const qint8 value = static_cast<qint8>(x * 127);
                *reinterpret_cast<quint8*>(ptr) = value;
            } else if (format.sampleSize() == 16 && format.sampleType() == QAudioFormat::UnSignedInt) {
                quint16 value = static_cast<quint16>((1.0 + x) / 2 * 65535);
                if (format.byteOrder() == QAudioFormat::LittleEndian)
                    qToLittleEndian<quint16>(value, ptr);
                else
                    qToBigEndian<quint16>(value, ptr);
            } else if (format.sampleSize() == 16 && format.sampleType() == QAudioFormat::SignedInt) {
                qint16 value = static_cast<qint16>(x * 32767);
                if (format.byteOrder() == QAudioFormat::LittleEndian)
                    qToLittleEndian<qint16>(value, ptr);
                else
                    qToBigEndian<qint16>(value, ptr);
            }

            ptr += channelBytes;
            length -= channelBytes;
        }
        ++sampleIndex;
    }
}
*/
