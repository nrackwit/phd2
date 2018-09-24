/*
*  guiding_assistant.cpp
*  PHD Guiding
*
*  Created by Andy Galasso and Bruce Waddington
*  Copyright (c) 2015 Andy Galasso and Bruce Waddington
*  All rights reserved.
*
*  This source code is distributed under the following "BSD" license
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions are met:
*    Redistributions of source code must retain the above copyright notice,
*     this list of conditions and the following disclaimer.
*    Redistributions in binary form must reproduce the above copyright notice,
*     this list of conditions and the following disclaimer in the
*     documentation and/or other materials provided with the distribution.
*    Neither the name of Craig Stark, Stark Labs nor the names of its
*     contributors may be used to endorse or promote products derived from
*     this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
*  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
*  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
*  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
*  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
*  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
*  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
*  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
*  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
*  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*
*/

#include "phd.h"
#include "guiding_assistant.h"
#include "backlash_comp.h"
#include "guiding_stats.h"

#include <wx/textwrapper.h>

inline static void StartRow(int& row, int& column)
{
    ++row;
    column = 0;
}

static void MakeBold(wxControl *ctrl)
{
    wxFont font = ctrl->GetFont();
    font.SetWeight(wxFONTWEIGHT_BOLD);
    ctrl->SetFont(font);
}

// Dialog for making sure sampling period is adequate for decent measurements
struct SampleWait : public wxDialog
{
    wxStaticText *m_CountdownAmount;
    wxTimer m_SecondsTimer;
    int m_SecondsLeft;

    SampleWait(int SamplePeriod, bool BltNeeded);
    void OnTimer(wxTimerEvent& evt);
    void OnCancel(wxCommandEvent& event);
};

SampleWait::SampleWait(int SecondsLeft, bool BltNeeded) : wxDialog(pFrame, wxID_ANY, _("Extended Sampling"))
{
    wxBoxSizer* vSizer = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* amtSizer = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText* explanation = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    wxString msg;
    if (BltNeeded)
        msg = _("Additional data sampling is being done to better meaure Dec drift. Backlash testing \nwill start automatically when sampling is completed.");
    else
        msg = _("Additional sampling is being done for accurate measurements.  Results will be shown when sampling is complete.");
    explanation->SetLabelText(msg);
    MakeBold(explanation);
    wxStaticText* countDownLabel = new wxStaticText(this, wxID_ANY, _("Seconds remaining: "), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    m_SecondsLeft = SecondsLeft;
    m_CountdownAmount = new wxStaticText(this, wxID_ANY, std::to_string(wxMax(0, m_SecondsLeft)));
    amtSizer->Add(countDownLabel, wxSizerFlags(0).Border(wxALL, 8));
    amtSizer->Add(m_CountdownAmount, wxSizerFlags(0).Border(wxALL, 8));
    wxButton* cancelBtn = new wxButton(this, wxID_ANY, _("Cancel"));
    cancelBtn->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(SampleWait::OnCancel), NULL, this);
    wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);
    btnSizer->Add(cancelBtn, wxSizerFlags(0).Border(wxALL, 8).Center());

    vSizer->Add(explanation, wxSizerFlags(0).Border(wxALL, 8).Center());
    vSizer->Add(amtSizer, wxSizerFlags(0).Border(wxALL, 8).Center());
    vSizer->Add(btnSizer, wxSizerFlags(0).Border(wxALL, 8).Center());

    SetAutoLayout(true);
    SetSizerAndFit(vSizer);

    m_SecondsTimer.Connect(wxEVT_TIMER, wxTimerEventHandler(SampleWait::OnTimer), NULL, this);
    m_SecondsTimer.Start(1000);
}

void SampleWait::OnTimer(wxTimerEvent& evt)
{
    m_SecondsLeft -= 1;
    if (m_SecondsLeft > 0)
    {
        m_CountdownAmount->SetLabelText(std::to_string(m_SecondsLeft));
        m_CountdownAmount->Update();
    }
    else
    {
        m_SecondsTimer.Stop();
        EndDialog(wxOK);
    }
}

void SampleWait::OnCancel(wxCommandEvent& event)
{
    m_SecondsTimer.Stop();
    if (wxGetKeyState(WXK_CONTROL))
        EndDialog(wxOK);
    else
        EndDialog(wxCANCEL);

}

// Encapsulated struct for implementing the dialog box
struct GuidingAsstWin : public wxDialog
{
    enum DialogState
    {
        STATE_NO_STAR = 0,
        STATE_START_READY = 1,
        STATE_MEASURING = 2,
        STATE_STOPPED = 3
    };
    enum DlgConstants {MAX_BACKLASH_COMP = 3000, GA_MIN_SAMPLING_PERIOD = 120};

    wxButton *m_start;
    wxButton *m_stop;
    wxTextCtrl *m_report;
    wxStaticText *m_instructions;
    wxGrid *m_statusgrid;
    wxGrid *m_displacementgrid;
    wxGrid *m_othergrid;
    wxFlexGridSizer *m_recommendgrid;
    wxBoxSizer *m_vSizer;
    wxStaticBoxSizer *m_recommend_group;
    wxCheckBox *m_backlashCB;
    wxStaticText *m_gaStatus;
    wxButton *m_graphBtn;

    wxGridCellCoords m_timestamp_loc;
    wxGridCellCoords m_starmass_loc;
    wxGridCellCoords m_samplecount_loc;
    wxGridCellCoords m_snr_loc;
    wxGridCellCoords m_elapsedtime_loc;
    wxGridCellCoords m_exposuretime_loc;
    wxGridCellCoords m_hfcutoff_loc;
    wxGridCellCoords m_ra_rms_loc;
    wxGridCellCoords m_dec_rms_loc;
    wxGridCellCoords m_total_rms_loc;
    wxGridCellCoords m_ra_peak_loc;
    wxGridCellCoords m_dec_peak_loc;
    wxGridCellCoords m_ra_peakpeak_loc;
    wxGridCellCoords m_ra_drift_loc;
    wxGridCellCoords m_ra_drift_exp_loc;
    wxGridCellCoords m_dec_drift_loc;
    wxGridCellCoords m_pae_loc;
    wxGridCellCoords m_ra_peak_drift_loc;
    wxGridCellCoords m_backlash_loc;
    wxButton *m_raMinMoveButton;
    wxButton *m_decMinMoveButton;
    wxButton *m_decBacklashButton;
    wxStaticText *m_ra_msg;
    wxStaticText *m_dec_msg;
    wxStaticText *m_snr_msg;
    wxStaticText *m_pae_msg;
    wxStaticText *m_backlash_msg;
    wxStaticText *m_exposure_msg;
    wxStaticText *m_calibration_msg;
    double m_ra_minmove_rec;  // recommended value
    double m_dec_minmove_rec; // recommended value
    double m_min_exp_rec;
    double m_max_exp_rec;

    DialogState m_dlgState;
    bool m_measuring;
    wxLongLong_t m_startTime;
    long m_elapsedSecs;
    PHD_Point m_startPos;
    wxString startStr;
    DescriptiveStats m_hpfRAStats;
    DescriptiveStats m_lpfRAStats;
    DescriptiveStats m_hpfDecStats;
    AxisStats m_decAxisStats;
    AxisStats m_raAxisStats;
    long m_axisTimebase;
    HighPassFilter m_raHPF;
    LowPassFilter m_raLPF;
    HighPassFilter m_decHPF;
    double sumSNR;
    double sumMass;
    double m_lastTime;
    double maxRateRA;               // arc-sec per second
    double decDriftPerMin;          // px per minute
    double decCorrectedRMS;         // RMS of drift-corrected Dec dataset
    double alignmentError;          // arc-minutes
    double m_backlashPx;
    int m_backlashMs;
    double m_backlashSigmaMs;
    int m_backlashRecommendedMs;

    bool m_guideOutputDisabled;
    bool m_savePrimaryMountEnabled;
    bool m_saveSecondaryMountEnabled;
    bool m_measurementsTaken;
    int  m_origSubFrames;
    bool m_suspectCalibration;

    bool m_measuringBacklash;

    BacklashTool *m_backlashTool;

    GuidingAsstWin();
    ~GuidingAsstWin();

    void OnClose(wxCloseEvent& event);
    void OnMouseMove(wxMouseEvent&);
    void OnAppStateNotify(wxCommandEvent& event);
    void OnStart(wxCommandEvent& event);
    void DoStop(const wxString& status = wxEmptyString);
    void OnStop(wxCommandEvent& event);
    void OnRAMinMove(wxCommandEvent& event);
    void OnDecMinMove(wxCommandEvent& event);
    void OnDecBacklash(wxCommandEvent& event);
    void OnGraph(wxCommandEvent& event);
    void OnHelp(wxCommandEvent& event);

    wxStaticText *AddRecommendationEntry(const wxString& msg, wxObjectEventFunction handler, wxButton **ppButton);
    wxStaticText *AddRecommendationEntry(const wxString& msg);
    void FillResultCell(wxGrid *pGrid, const wxGridCellCoords& loc, double pxVal, double asVal, const wxString& units1, const wxString& units2, const wxString& extraInfo = wxEmptyString);
    void UpdateInfo(const GuideStepInfo& info);
    void FillInstructions(DialogState eState);
    void MakeRecommendations();
    void LogResults();
    void BacklashStep(const PHD_Point& camLoc);
    void EndBacklashTest(bool completed);
    void BacklashError();
    void StatsReset();
};

static void HighlightCell(wxGrid *pGrid, wxGridCellCoords where)
{
    pGrid->SetCellBackgroundColour(where.GetRow(), where.GetCol(), "DARK SLATE GREY");
    pGrid->SetCellTextColour(where.GetRow(), where.GetCol(), "white");
}

struct GridTooltipInfo : public wxObject
{
    wxGrid *grid;
    int gridNum;
    wxGridCellCoords prevCoords;
    GridTooltipInfo(wxGrid *g, int i) : grid(g), gridNum(i) { }
};

struct TextWrapper
{
    wxWindow *win;
    int width;
    TextWrapper(wxWindow *win_, int width_) : win(win_), width(width_) { }
    wxString Wrap(const wxString& text) const
    {
        struct Wrapper : public wxTextWrapper
        {
            wxString str;
            Wrapper(wxWindow *win_, const wxString& text, int width_) { Wrap(win_, text, width_); }
            void OnOutputLine(const wxString& line) { str += line; }
            void OnNewLine() { str += '\n'; }
        };
        return Wrapper(win, text, width).str;
    }
};

// Constructor
GuidingAsstWin::GuidingAsstWin()
    : wxDialog(pFrame, wxID_ANY, wxGetTranslation(_("Guiding Assistant"))),
      m_measuring(false),
      m_guideOutputDisabled(false),
      m_measurementsTaken(false),
      m_origSubFrames(-1)
{
    // Sizer hierarchy:
    // m_vSizer has {instructions, vResultsSizer, m_gaStatus, btnSizer}
    // vResultsSizer has {hTopSizer, hBottomSizer}
    // hTopSizer has {status_group, displacement_group}
    // hBottomSizer has {other_group, m_recommendation_group}
    m_vSizer = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* vResultsSizer = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* hTopSizer = new wxBoxSizer(wxHORIZONTAL);       // Measurement status and high-frequency results
    wxBoxSizer* hBottomSizer = new wxBoxSizer(wxHORIZONTAL);             // Low-frequency results and recommendations

    m_instructions = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(700, 50), wxALIGN_LEFT | wxST_NO_AUTORESIZE);
    MakeBold(m_instructions);
    m_vSizer->Add(m_instructions, wxSizerFlags(0).Border(wxALL, 8));

    // Grids have either 3 or 4 columns, so compute width of largest label as scaling term for column widths
    double minLeftCol = StringWidth(this,
        _(" -999.99 px/min (-999.99 arc-sec/min )")) + 6;
    double minRightCol = 1.25 * (StringWidth(this,
        _(" 9.99 px ( 9.99 arc-sec)")) + 6);
    // Start of status group
    wxStaticBoxSizer *status_group = new wxStaticBoxSizer(wxVERTICAL, this, _("Measurement Status"));
    m_statusgrid = new wxGrid(this, wxID_ANY);
    m_statusgrid->CreateGrid(3, 4);
    m_statusgrid->GetGridWindow()->Bind(wxEVT_MOTION, &GuidingAsstWin::OnMouseMove, this, wxID_ANY, wxID_ANY, new GridTooltipInfo(m_statusgrid, 1));
    m_statusgrid->SetRowLabelSize(1);
    m_statusgrid->SetColLabelSize(1);
    m_statusgrid->EnableEditing(false);
    m_statusgrid->SetDefaultColSize((round(2.0 * minLeftCol / 4.0) + 0.5));

    int col = 0;
    int row = 0;
    m_statusgrid->SetCellValue(row, col++, _("Start time"));
    m_timestamp_loc.Set(row, col++);
    m_statusgrid->SetCellValue(row, col++, _("Exposure time"));
    m_exposuretime_loc.Set(row, col++);

    StartRow(row, col);
    m_statusgrid->SetCellValue(row, col++, _("SNR"));
    m_snr_loc.Set(row, col++);
    m_statusgrid->SetCellValue(row, col++, _("Star mass"));
    m_starmass_loc.Set(row, col++);

    StartRow(row, col);
    m_statusgrid->SetCellValue(row, col++, _("Elapsed time"));
    m_elapsedtime_loc.Set(row, col++);
    m_statusgrid->SetCellValue(row, col++, _("Sample count"));
    m_samplecount_loc.Set(row, col++);

    //StartRow(row, col);
    //m_statusgrid->SetCellValue(_("Frequency cut-off:"), row, col++);   // Leave out for now, probably not useful to users
    //m_hfcutoff_loc.Set(row, col++);

    status_group->Add(m_statusgrid);
    hTopSizer->Add(status_group, wxSizerFlags(0).Border(wxALL, 8));
    // End of status group

    // Start of star displacement group
    wxStaticBoxSizer *displacement_group = new wxStaticBoxSizer(wxVERTICAL, this, _("High-frequency Star Motion"));
    m_displacementgrid = new wxGrid(this, wxID_ANY);
    m_displacementgrid->CreateGrid(3, 2);
    m_displacementgrid->GetGridWindow()->Bind(wxEVT_MOTION, &GuidingAsstWin::OnMouseMove, this, wxID_ANY, wxID_ANY, new GridTooltipInfo(m_displacementgrid, 2));
    m_displacementgrid->SetRowLabelSize(1);
    m_displacementgrid->SetColLabelSize(1);
    m_displacementgrid->EnableEditing(false);
    m_displacementgrid->SetDefaultColSize(minRightCol);

    row = 0;
    col = 0;
    m_displacementgrid->SetCellValue(row, col++, _("Right ascension, RMS"));
    m_ra_rms_loc.Set(row, col++);

    StartRow(row, col);
    m_displacementgrid->SetCellValue(row, col++, _("Declination, RMS"));
    m_dec_rms_loc.Set(row, col++);

    StartRow(row, col);
    m_displacementgrid->SetCellValue(row, col++, _("Total, RMS"));
    m_total_rms_loc.Set(row, col++);

    displacement_group->Add(m_displacementgrid);
    hTopSizer->Add(displacement_group, wxSizerFlags(0).Border(wxALL, 8));
    vResultsSizer->Add(hTopSizer);
    // End of displacement group

    // Start of "Other" (peak and drift) group
    wxStaticBoxSizer *other_group = new wxStaticBoxSizer(wxVERTICAL, this, _("Other Star Motion"));
    m_othergrid = new wxGrid(this, wxID_ANY);
    m_othergrid->CreateGrid(9, 2);
    m_othergrid->GetGridWindow()->Bind(wxEVT_MOTION, &GuidingAsstWin::OnMouseMove, this, wxID_ANY, wxID_ANY, new GridTooltipInfo(m_othergrid, 3));
    m_othergrid->SetRowLabelSize(1);
    m_othergrid->SetColLabelSize(1);
    m_othergrid->EnableEditing(false);
    m_othergrid->SetDefaultColSize(minLeftCol);

    TextWrapper w(this, minLeftCol);

    row = 0;
    col = 0;
    m_othergrid->SetCellValue(row, col++, w.Wrap(_("Right ascension, Peak")));
    m_ra_peak_loc.Set(row, col++);

    StartRow(row, col);
    m_othergrid->SetCellValue(row, col++, w.Wrap(_("Declination, Peak")));
    m_dec_peak_loc.Set(row, col++);

    StartRow(row, col);
    m_othergrid->SetCellValue(row, col++, w.Wrap(_("Right ascension, Peak-Peak")));
    m_ra_peakpeak_loc.Set(row, col++);

    StartRow(row, col);
    m_othergrid->SetCellValue(row, col++, w.Wrap(_("Right ascension Drift Rate")));
    m_ra_drift_loc.Set(row, col++);

    StartRow(row, col);
    m_othergrid->SetCellValue(row, col++, w.Wrap(_("Right ascension Max Drift Rate")));
    m_ra_peak_drift_loc.Set(row, col++);

    StartRow(row, col);
    m_othergrid->SetCellValue(row, col++, w.Wrap(_("Drift-limiting exposure")));
    m_ra_drift_exp_loc.Set(row, col++);

    StartRow(row, col);
    m_othergrid->SetCellValue(row, col++, w.Wrap(_("Declination Drift Rate")));
    m_dec_drift_loc.Set(row, col++);

    StartRow(row, col);
    m_othergrid->SetCellValue(row, col++, w.Wrap(_("Declination Backlash")));
    m_backlash_loc.Set(row, col++);

    StartRow(row, col);
    m_othergrid->SetCellValue(row, col++, w.Wrap(_("Polar Alignment Error")));
    m_pae_loc.Set(row, col++);

    m_othergrid->AutoSizeColumn(0);
    m_othergrid->AutoSizeRows();

    other_group->Add(m_othergrid);
    hBottomSizer->Add(other_group, wxSizerFlags(0).Border(wxALL, 8));
    // End of peak and drift group

    // Start of Recommendations group - just a place-holder for layout, populated in MakeRecommendations
    m_recommend_group = new wxStaticBoxSizer(wxVERTICAL, this, _("Recommendations"));
    m_recommendgrid = new wxFlexGridSizer(2, 0, 0);
    m_recommendgrid->AddGrowableCol(0);
    m_ra_msg = NULL;
    m_dec_msg = NULL;
    m_snr_msg = NULL;
    m_backlash_msg = NULL;
    m_pae_msg = NULL;
    m_exposure_msg = NULL;
    m_calibration_msg = NULL;

    m_recommend_group->Add(m_recommendgrid, wxSizerFlags(1).Expand());
    // Add buttons for viewing the Dec backlash graph or getting help
    wxBoxSizer* hBtnSizer = new wxBoxSizer(wxHORIZONTAL);
    m_graphBtn = new wxButton(this, wxID_ANY, _("Show Backlash Graph"));
    m_graphBtn->SetToolTip(_("Show graph of backlash measurement points"));
    m_graphBtn->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(GuidingAsstWin::OnGraph), NULL, this);
    m_graphBtn->Enable(false);
    hBtnSizer->Add(m_graphBtn, wxSizerFlags(0).Border(wxALL, 5));
    wxButton* helpBtn = new wxButton(this, wxID_ANY, _("Help"));
    helpBtn->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(GuidingAsstWin::OnHelp), NULL, this);
    hBtnSizer->Add(50, 0);
    hBtnSizer->Add(helpBtn, wxSizerFlags(0).Border(wxALL, 5));
    m_recommend_group->Add(hBtnSizer, wxSizerFlags(0).Border(wxALL, 5));
    // Recommendations will be hidden/shown depending on state
    hBottomSizer->Add(m_recommend_group, wxSizerFlags(0).Border(wxALL, 8));
    vResultsSizer->Add(hBottomSizer);

    m_vSizer->Add(vResultsSizer);
    m_recommend_group->Show(false);
    // End of recommendations

    m_backlashCB = new wxCheckBox(this, wxID_ANY, _("Measure Declination Backlash"));
    m_backlashCB->SetToolTip(_("PHD2 will move the guide star a considerable distance north, then south to measure backlash. Be sure the selected star has "
        "plenty of room to move in the north direction.  If the guide star is lost, increase the size of the search region to at least 20 px"));
    if (!pMount->IsStepGuider())
    {
        m_backlashCB->SetValue(true);
        m_backlashCB->Enable(true);
    }
    else
    {
        m_backlashCB->SetValue(false);
        m_backlashCB->Enable(false);
    }
    // Text area for showing backlash measuring steps
    m_gaStatus = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(500, 40), wxALIGN_CENTER);
    MakeBold(m_gaStatus);
    m_vSizer->Add(m_gaStatus, wxSizerFlags(0).Border(wxALL, 8).Center());

    wxBoxSizer *btnSizer = new wxBoxSizer(wxHORIZONTAL);
    btnSizer->Add(10, 0);       // a little spacing left of Start button
    btnSizer->Add(m_backlashCB, wxSizerFlags(0).Border(wxALL, 8));
    btnSizer->Add(40, 0);       // Put a spacer between the button and checkbox

    m_start = new wxButton(this, wxID_ANY, _("Start"), wxDefaultPosition, wxDefaultSize, 0);
    m_start->SetToolTip(_("Start measuring (disables guiding)"));
    btnSizer->Add(m_start, 0, wxALL, 5);
    m_start->Enable(false);

    m_stop = new wxButton(this, wxID_ANY, _("Stop"), wxDefaultPosition, wxDefaultSize, 0);
    m_stop->SetToolTip(_("Stop measuring and re-enable guiding"));
    m_stop->Enable(false);

    btnSizer->Add(m_stop, 0, wxALL, 5);
    m_vSizer->Add(btnSizer, 0, wxEXPAND, 5);

    SetAutoLayout(true);
    SetSizerAndFit(m_vSizer);

    Connect(wxEVT_CLOSE_WINDOW, wxCloseEventHandler(GuidingAsstWin::OnClose));
    Connect(APPSTATE_NOTIFY_EVENT, wxCommandEventHandler(GuidingAsstWin::OnAppStateNotify));
    m_start->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(GuidingAsstWin::OnStart), NULL, this);
    m_stop->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(GuidingAsstWin::OnStop), NULL, this);

    m_backlashTool = new BacklashTool();
    m_measuringBacklash = false;

    int xpos = pConfig->Global.GetInt("/GuidingAssistant/pos.x", -1);
    int ypos = pConfig->Global.GetInt("/GuidingAssistant/pos.y", -1);
    MyFrame::PlaceWindowOnScreen(this, xpos, ypos);

    wxCommandEvent dummy;
    OnAppStateNotify(dummy); // init state-dependent controls

    if (pFrame->pGuider->IsGuiding())
    {
        OnStart(dummy);             // Auto-start if we're already guiding
    }
}

GuidingAsstWin::~GuidingAsstWin(void)
{
    pFrame->pGuidingAssistant = 0;
    delete m_backlashTool;
}

void GuidingAsstWin::StatsReset()
{
    m_hpfRAStats.ClearAll();
    m_lpfRAStats.ClearAll();
    m_hpfDecStats.ClearAll();
    m_decAxisStats.ClearAll();
    m_raAxisStats.ClearAll();
}

static bool GetGridToolTip(int gridNum, const wxGridCellCoords& coords, wxString *s)
{
    int col = coords.GetCol();

    if (gridNum > 1 && col != 0)
        return false;
    else
    if (col != 0 && col != 2)
        return false;

    switch (gridNum * 100 + coords.GetRow())
    {
        // status grid
        case 101:
        {
            if (col == 0)
                *s = _("Signal-to-noise ratio; a measure of how well PHD2 can isolate the star from the sky/noise background");
            else
                *s = _("Measure of overall star brightness. Consider using 'Auto-select Star' (Alt-S) to choose the star.");
            break;
        }

        // displacement grid
        case 200: *s = _("Measure of typical high-frequency right ascension star movements; guiding usually cannot correct for fluctuations this small."); break;
        case 201: *s = _("Measure of typical high-frequency declination star movements; guiding usually cannot correct for fluctuations this small."); break;

        // other grid
        case 300: *s = _("Maximum sample-sample deflection seen in right ascension."); break;
        case 301: *s = _("Maximum sample-sample deflection seen in declination."); break;
        case 302: *s = _("Maximum peak-peak deflection seen in right ascension during sampling period."); break;
        case 303: *s = _("Estimated overall drift rate in right ascension."); break;
        case 304: *s = _("Maximum drift rate in right ascension during sampling period."); break;
        case 305: *s = _("Exposure time to keep maximum RA drift below the recommended min-move level."); break;
        case 306: *s = _("Estimated overall drift rate in declination."); break;
        case 307: *s = _("Estimated declination backlash if test was completed. Results are time to clear backlash (ms) and corresponding gear angle (arc-sec). Uncertainty estimate is one unit of standard deviation"); break;
        case 308: *s = _("Estimate of polar alignment error. If the scope declination is unknown, the value displayed is a lower bound and the actual error may be larger."); break;

        default: return false;
    }

    return true;
}

void GuidingAsstWin::OnMouseMove(wxMouseEvent& ev)
{
    GridTooltipInfo *info = static_cast<GridTooltipInfo *>(ev.GetEventUserData());
    wxGridCellCoords coords(info->grid->XYToCell(info->grid->CalcUnscrolledPosition(ev.GetPosition())));
    if (coords != info->prevCoords)
    {
        info->prevCoords = coords;
        wxString s;
        if (GetGridToolTip(info->gridNum, coords, &s))
            info->grid->GetGridWindow()->SetToolTip(s);
        else
            info->grid->GetGridWindow()->UnsetToolTip();
    }
    ev.Skip();
}

void GuidingAsstWin::FillInstructions(DialogState eState)
{
    wxString instr;

    switch (eState)
    {
    case STATE_NO_STAR:
        instr = _("Choose a non-saturated star with a good SNR (>= 8) and begin guiding");
        break;
    case STATE_START_READY:
        if (!m_measurementsTaken)
            instr = _("Click Start to begin measurements.  Guiding will be disabled during this time, so the star will move around.");
        else
            instr = m_instructions->GetLabel();
        break;
    case STATE_MEASURING:
        instr = _("Guiding output is disabled and star movement is being measured.  Click Stop when the RMS and polar alignment values have stabilized (at least 2 minutes).");
        break;
    case STATE_STOPPED:
        instr = _("Guiding has been resumed. Look at the recommendations and make any desired changes.  Click Start to repeat the measurements, or close the window to continue guiding.");
        break;
    }
    m_instructions->SetLabel(instr);
    m_instructions->Wrap(700);
    m_instructions->Layout();
}

void GuidingAsstWin::BacklashStep(const PHD_Point& camLoc)
{
    BacklashTool::MeasurementResults qual;
    m_backlashTool->DecMeasurementStep(camLoc);
    wxString bl_msg = _("Measuring backlash: ") + m_backlashTool->GetLastStatus();
    m_gaStatus->SetLabel(bl_msg);
    if (m_backlashTool->GetBltState() == BacklashTool::BLT_STATE_COMPLETED)
    {
        m_backlashTool->DecMeasurementStep(camLoc);
        wxString bl_msg = _("Measuring backlash: ") + m_backlashTool->GetLastStatus();
        m_gaStatus->SetLabel(bl_msg);
        if (m_backlashTool->GetBltState() == BacklashTool::BLT_STATE_COMPLETED)
        {
            qual = m_backlashTool->GetMeasurementQuality();
            if (qual == BacklashTool::MEASUREMENT_VALID || qual == BacklashTool::MEASUREMENT_TOO_FEW_NORTH)
            {
                double bltSigmaPx;
                double bltGearAngle;
                double bltGearAngleSigma;
                // populate result variables
                m_backlashPx = m_backlashTool->GetBacklashResultPx();
                m_backlashMs = m_backlashTool->GetBacklashResultMs();
                m_backlashTool->GetBacklashSigma(&bltSigmaPx, &m_backlashSigmaMs);
                bltGearAngle = (m_backlashPx * pFrame->GetCameraPixelScale());
                bltGearAngleSigma = (bltSigmaPx * pFrame->GetCameraPixelScale());
                wxString preamble = ((m_backlashMs >= 5000 || qual == BacklashTool::MEASUREMENT_TOO_FEW_NORTH) ? ">=" : "");
                wxString MSEC(_("ms"));
                wxString ARCSEC(_("arc-sec"));
                wxString outStr;
                if (qual == BacklashTool::MEASUREMENT_VALID)
                    outStr = wxString::Format("%s %d \u00B1 %0.0f %s (%0.1f \u00B1 %0.1f %s)",
                    preamble, wxMax(0, m_backlashMs), m_backlashSigmaMs, MSEC,
                    wxMax(0, bltGearAngle), bltGearAngleSigma, ARCSEC);
                else
                    outStr = wxString::Format("%s %d \u00B1 %s",
                        preamble, wxMax(0, m_backlashMs), MSEC + _(" (test impaired)"));
                m_othergrid->SetCellValue(m_backlash_loc, outStr);
                HighlightCell(m_othergrid, m_backlash_loc);
                outStr += "\n";
                GuideLog.NotifyGAResult("Backlash=" + outStr);
                Debug.Write("BLT: Reported result = " + outStr);
                m_graphBtn->Enable(true);
            }
            else
            {
                m_othergrid->SetCellValue(m_backlash_loc, "");
            }
            EndBacklashTest(qual == BacklashTool::MEASUREMENT_VALID || qual == BacklashTool::MEASUREMENT_TOO_FEW_NORTH);
        }
    }
    else
    if (m_backlashTool->GetBltState() == BacklashTool::BLT_STATE_ABORTED)
        EndBacklashTest(false);
}

void GuidingAsstWin::BacklashError()
{
    EndBacklashTest(false);
}

// Event handlers for applying recommendations
void GuidingAsstWin::OnRAMinMove(wxCommandEvent& event)
{
    GuideAlgorithm *raAlgo = pMount->GetXGuideAlgorithm();

    if (!raAlgo)
        return;

    if (raAlgo->GetMinMove() >= 0.0)
    {
        if (!raAlgo->SetMinMove(m_ra_minmove_rec))
        {
            Debug.Write(wxString::Format("GuideAssistant changed RA_MinMove to %0.2f\n", m_ra_minmove_rec));
            pFrame->pGraphLog->UpdateControls();
            pFrame->NotifyGuidingParam("RA " + raAlgo->GetGuideAlgorithmClassName() + " MinMove ", m_ra_minmove_rec);
            m_raMinMoveButton->Enable(false);
        }
        else
            Debug.Write("GuideAssistant could not change RA_MinMove\n");
    }
    else
        Debug.Write("GuideAssistant logic flaw, RA algorithm has no MinMove property\n");
}

void GuidingAsstWin::OnDecMinMove(wxCommandEvent& event)
{
    GuideAlgorithm *decAlgo = pMount->GetYGuideAlgorithm();

    if (!decAlgo)
        return;

    if (decAlgo->GetMinMove() >= 0.0)
    {
        if (!decAlgo->SetMinMove(m_dec_minmove_rec))
        {
            Debug.Write(wxString::Format("GuideAssistant changed Dec_MinMove to %0.2f\n", m_dec_minmove_rec));
            pFrame->pGraphLog->UpdateControls();
            pFrame->NotifyGuidingParam("Declination " + decAlgo->GetGuideAlgorithmClassName() + " MinMove ", m_dec_minmove_rec);
            m_decMinMoveButton->Enable(false);
        }
        else
            Debug.Write("GuideAssistant could not change Dec_MinMove\n");
    }
    else
        Debug.Write("GuideAssistant logic flaw, Dec algorithm has no MinMove property\n");
}

void GuidingAsstWin::OnDecBacklash(wxCommandEvent& event)
{
    BacklashComp *pComp = TheScope()->GetBacklashComp();

    pComp->SetBacklashPulse(m_backlashRecommendedMs, false);
    pComp->EnableBacklashComp(!pMount->IsStepGuider());
    m_decBacklashButton->Enable(false);
}

void GuidingAsstWin::OnGraph(wxCommandEvent& event)
{
    m_backlashTool->ShowGraph(this);
}

void GuidingAsstWin::OnHelp(wxCommandEvent& event)
{
    pFrame->help->Display("Tools.htm#Guiding_Assistant");   // named anchors in help file are not subject to translation
}

// Adds a recommendation string and a button bound to the passed event handler
wxStaticText *GuidingAsstWin::AddRecommendationEntry(const wxString& msg, wxObjectEventFunction handler, wxButton **ppButton)
{
    wxStaticText *rec_label;

    rec_label = new wxStaticText(this, wxID_ANY, msg);
    rec_label->Wrap(250);
    m_recommendgrid->Add(rec_label, 1, wxALIGN_LEFT | wxALL, 5);
    if (handler)
    {
        int min_h;
        int min_w;
        this->GetTextExtent(_("Apply"), &min_w, &min_h);
        *ppButton = new wxButton(this, wxID_ANY, _("Apply"), wxDefaultPosition, wxSize(min_w + 8, min_h + 8), 0);
        m_recommendgrid->Add(*ppButton, 0, wxALIGN_RIGHT | wxALL, 5);
        (*ppButton)->Connect(wxEVT_COMMAND_BUTTON_CLICKED, handler, NULL, this);
    }
    else
    {
        wxStaticText *rec_tmp = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize);
        m_recommendgrid->Add(rec_tmp, 0, wxALL, 5);
    }
    return rec_label;
}

// Jacket for simple addition of a text-only recommendation
wxStaticText *GuidingAsstWin::AddRecommendationEntry(const wxString& msg)
{
    return AddRecommendationEntry(msg, NULL, NULL);
}

void GuidingAsstWin::LogResults()
{
    wxString str;
    Debug.Write("Guiding Assistant results follow:\n");
    str = wxString::Format("SNR=%s, Samples=%s, Elapsed Time=%s, RA HPF-RMS=%s, Dec HPF-RMS=%s, Total HPF-RMS=%s\n",
        m_statusgrid->GetCellValue(m_snr_loc), m_statusgrid->GetCellValue(m_samplecount_loc), m_statusgrid->GetCellValue(m_elapsedtime_loc),
        m_displacementgrid->GetCellValue(m_ra_rms_loc),
        m_displacementgrid->GetCellValue(m_dec_rms_loc), m_displacementgrid->GetCellValue(m_total_rms_loc));
    GuideLog.NotifyGAResult(str);
    Debug.Write(str);
    str = wxString::Format("RA Peak=%s, RA Peak-Peak %s, RA Drift Rate=%s, Max RA Drift Rate=%s, Drift-Limiting Exp=%s\n",
        m_othergrid->GetCellValue(m_ra_peak_loc),
        m_othergrid->GetCellValue(m_ra_peakpeak_loc), m_othergrid->GetCellValue(m_ra_drift_loc),
        m_othergrid->GetCellValue(m_ra_peak_drift_loc),
        m_othergrid->GetCellValue(m_ra_drift_exp_loc)
        );
    GuideLog.NotifyGAResult(str);
    Debug.Write(str);
    str = wxString::Format("Dec Drift Rate=%s, Dec Peak=%s, PA Error=%s\n",
        m_othergrid->GetCellValue(m_dec_drift_loc), m_othergrid->GetCellValue(m_dec_peak_loc),
        m_othergrid->GetCellValue(m_pae_loc));
    GuideLog.NotifyGAResult(str);
    Debug.Write(str);

}

static wxString SizedMsg(wxString msg)
{
    wxString padding = "                                        ";
    if (msg.length() < 70)
        return msg += padding.SubString(0, 70 - msg.length());
    else
        return msg;
}

void GuidingAsstWin::MakeRecommendations()
{
    double pxscale = pFrame->GetCameraPixelScale();
    double rarms = m_hpfRAStats.GetSigma();
    double ramean = m_hpfDecStats.GetMean();
    // Using linear-fit, get the drift-corrected Dec rms for things like min-move estimates
    double slope = 0;
    double intcpt = 0;
    if (m_decAxisStats.GetCount() > 1)
        m_decAxisStats.GetLinearFitResults(&slope, &intcpt, &decCorrectedRMS);

    double multiplier_ra  = 1.0;   // 66% prediction interval
    double multiplier_dec = (pxscale < 1.5) ? 1.28 : 1.65;          // 20% or 10% activity target based on normal distribution
    double ideal_min_exposure;
    double ideal_max_exposure;
    double min_rec_range = 2.0;
    // round up to next multiple of .05, but do not go below 0.10 pixel
    double const unit = 0.05;
    //double rounded_rarms = std::max(round(rarms * multiplier_ra / unit + 0.5) * unit, 0.10);
    double rounded_decrms = std::max(round(decCorrectedRMS * multiplier_dec / unit + 0.5) * unit, 0.10);
    CalibrationDetails calDetails;
    wxString logStr;

    TheScope()->GetCalibrationDetails(&calDetails);
    m_suspectCalibration = calDetails.lastIssue != CI_None || m_backlashTool->GetBacklashExempted();

    m_dec_minmove_rec = rounded_decrms;
    m_ra_minmove_rec = m_dec_minmove_rec * multiplier_ra / multiplier_dec;
    // Need to apply some constraints on the relative ratios because the ra_rms stat can be affected by large PE or drift
    m_ra_minmove_rec = wxMin(wxMax(m_ra_minmove_rec, 0.8 * m_dec_minmove_rec), 1.2 * m_dec_minmove_rec);        // within 20% of dec recommendation
    // Refine the drift-limiting exposure value based on the ra_min_move recommendation
    m_othergrid->SetCellValue(m_ra_drift_exp_loc, maxRateRA <= 0.0 ? _(" ") :
        wxString::Format("%6.1f %s ", m_ra_minmove_rec / maxRateRA, (_("s"))));

    LogResults();               // Dump the raw statistics
    Debug.Write(wxString::Format("Linear-fit Dec drift=%0.3f px/min, Drift-corrected Dec(raw) RMS=%0.3fpx\n", decDriftPerMin, decCorrectedRMS));

    // Clump the no-button messages at the top
    // ideal exposure ranges in general
    ideal_min_exposure = 2.0;
    ideal_max_exposure = 4.0;
    // adjust the min-exposure downward if drift limiting exposure is lower; then adjust range accordingly
    double drift_exp;
    if (maxRateRA > 0)
        drift_exp = ceil((multiplier_ra * rarms / maxRateRA) / 0.5) * 0.5;                       // Rounded up to nearest 0.5 sec
    else
        drift_exp = ideal_min_exposure;
    m_min_exp_rec = std::max(1.0, std::min(drift_exp, ideal_min_exposure));                         // smaller of drift and ideal, never less than 1.0
    if (drift_exp > m_min_exp_rec)
    {
        if (drift_exp < ideal_max_exposure)
            m_max_exp_rec = std::max(drift_exp, m_min_exp_rec + min_rec_range);
        else
            m_max_exp_rec = ideal_max_exposure;
    }
    else
        m_max_exp_rec = m_min_exp_rec + min_rec_range;

    // Always make a recommendation on exposure times
    wxString msg = wxString::Format(_("Try to keep your exposure times in the range of %.1fs to %.1fs"), m_min_exp_rec, m_max_exp_rec);
    if (!m_exposure_msg)
        m_exposure_msg = AddRecommendationEntry(SizedMsg(msg));
    else
        m_exposure_msg->SetLabel(SizedMsg(msg));
    Debug.Write(wxString::Format("Recommendation: %s\n", msg));

    if (m_suspectCalibration)
    {
        wxString msg = _("Consider re-doing your calibration ");
        if (calDetails.lastIssue != CI_None)
            msg += _("(Prior alert)");
        else
            msg += _("(Backlash clearing)");
        if (!m_calibration_msg)
            m_calibration_msg = AddRecommendationEntry(SizedMsg(msg));
        else
            m_calibration_msg->SetLabel(SizedMsg(msg));
        logStr = wxString::Format("Recommendation: %s\n", msg);
        Debug.Write(logStr);
        GuideLog.NotifyGAResult(logStr);
    }

    if ((sumSNR / (double)m_lpfRAStats.GetCount()) < 5.0)
    {
        wxString msg(_("Consider using a brighter star for the test or increasing the exposure time"));
        if (!m_snr_msg)
            m_snr_msg = AddRecommendationEntry(SizedMsg(msg));
        else
            m_snr_msg->SetLabel(SizedMsg(msg));
        logStr = wxString::Format("Recommendation: %s\n", msg);
        Debug.Write(logStr);
        GuideLog.NotifyGAResult(logStr);
    }
    else
    {
        if (m_snr_msg)
            m_snr_msg->SetLabel(wxEmptyString);
    }

    if (alignmentError > 5.0)
    {
        wxString msg = alignmentError < 10.0 ?
            _("Polar alignment error > 5 arc-min; that could probably be improved.") :
            _("Polar alignment error > 10 arc-min; try using the Drift Align tool to improve alignment.");
        if (!m_pae_msg)
            m_pae_msg = AddRecommendationEntry(SizedMsg(msg));
        else
        {
            m_pae_msg->SetLabel(SizedMsg(msg));
            m_pae_msg->Wrap(400);
        }
        logStr = wxString::Format("Recommendation: %s\n", msg);
        Debug.Write(logStr);
        GuideLog.NotifyGAResult(logStr);
    }
    else
    {
        if (m_pae_msg)
            m_pae_msg->SetLabel(wxEmptyString);
    }

    if (pMount->GetXGuideAlgorithm() && pMount->GetXGuideAlgorithm()->GetMinMove() >= 0.0)
    {
        wxString msgText = wxString::Format(_("Try setting RA min-move to %0.2f"), m_ra_minmove_rec);
        if (!m_ra_msg)
        {
            m_ra_msg = AddRecommendationEntry(SizedMsg(msgText),
                wxCommandEventHandler(GuidingAsstWin::OnRAMinMove), &m_raMinMoveButton);
        }
        else
        {
            m_ra_msg->SetLabel(SizedMsg(msgText));
            m_raMinMoveButton->Enable(true);
        }
        logStr = wxString::Format("Recommendation: %s\n", msgText);
        Debug.Write(logStr);
        GuideLog.NotifyGAResult(logStr);
    }

    if (pMount->GetYGuideAlgorithm() && pMount->GetYGuideAlgorithm()->GetMinMove() >= 0.0)
    {
        wxString msgText = wxString::Format(_("Try setting Dec min-move to %0.2f"), m_dec_minmove_rec);
        if (!m_dec_msg)
        {
            m_dec_msg = AddRecommendationEntry(SizedMsg(msgText),
                wxCommandEventHandler(GuidingAsstWin::OnDecMinMove), &m_decMinMoveButton);
        }
        else
        {
            m_dec_msg->SetLabel(SizedMsg(msgText));
            m_decMinMoveButton->Enable(true);
        }
        logStr = wxString::Format("Recommendation: %s\n", msgText);
        Debug.Write(logStr);
        GuideLog.NotifyGAResult(logStr);
    }

    if (m_backlashTool->GetBltState() == BacklashTool::BLT_STATE_COMPLETED)
    {
        wxString msg;

        if (m_backlashMs > 0)
        {
            m_backlashRecommendedMs = (int)(floor(m_backlashMs / 10) * 10);        // round down to nearest 10ms
            m_backlashRecommendedMs = wxMax(m_backlashRecommendedMs, 10);
        }
        else
            m_backlashRecommendedMs = 0;
        bool largeBL = m_backlashMs > MAX_BACKLASH_COMP;
        if (m_backlashMs < 100)
            msg = _("Backlash is small, no compensation needed");              // assume it was a small measurement error
        else if (m_backlashMs <= MAX_BACKLASH_COMP)
            msg = wxString::Format(_("Try starting with a Dec backlash compensation of %d ms"), m_backlashRecommendedMs);
        else
        {
            msg = wxString::Format(_("Backlash is >= %d ms; you may need to guide in only one Dec direction (currently %s)"), m_backlashMs,
                decDriftPerMin >= 0 ? _("South") : _("North"));
        }

        if (!m_backlash_msg)
        {
            m_backlash_msg = AddRecommendationEntry(SizedMsg(msg), wxCommandEventHandler(GuidingAsstWin::OnDecBacklash), &m_decBacklashButton);
            m_decBacklashButton->Enable(!largeBL && m_backlashRecommendedMs > 100);
        }
        else
        {
            m_backlash_msg->SetLabel(SizedMsg(msg));
            m_decBacklashButton->Enable(!largeBL && m_backlashRecommendedMs > 100);
        }
        logStr = wxString::Format("Recommendation: %s\n", msg);
        Debug.Write(logStr);
        GuideLog.NotifyGAResult(logStr);
    }
    else
    {
        if (m_backlash_msg)
            m_backlash_msg->SetLabel(wxEmptyString);

    }

    m_recommend_group->Show(true);

    m_statusgrid->Layout();
    Layout();
    GetSizer()->Fit(this);
    Debug.Write("End of Guiding Assistant output....\n");
}

void GuidingAsstWin::OnStart(wxCommandEvent& event)
{
    if (!pFrame->pGuider->IsGuiding())
        return;

    double exposure = (double) pFrame->RequestedExposureDuration() / 1000.0;
    double lp_cutoff = wxMax(6.0, 3.0 * exposure);
    double hp_cutoff = 1.0;

    StatsReset();
    m_raHPF = HighPassFilter(hp_cutoff, exposure);
    m_raLPF = LowPassFilter(lp_cutoff, exposure);
    m_decHPF = HighPassFilter(hp_cutoff, exposure);

    sumSNR = sumMass = 0.0;

    m_start->Enable(false);
    m_stop->Enable(true);
    m_dlgState = STATE_MEASURING;
    FillInstructions(m_dlgState);
    m_gaStatus->SetLabel(_("Measuring..."));
    m_recommend_group->Show(false);
    HighlightCell(m_displacementgrid, m_ra_rms_loc);
    HighlightCell(m_displacementgrid, m_dec_rms_loc);
    HighlightCell(m_displacementgrid, m_total_rms_loc);

    Debug.AddLine("GuidingAssistant: Disabling guide output");

    if (pMount)
    {
        m_savePrimaryMountEnabled = pMount->GetGuidingEnabled();
        pMount->SetGuidingEnabled(false);
    }
    if (pSecondaryMount)
    {
        m_saveSecondaryMountEnabled = pSecondaryMount->GetGuidingEnabled();
        pSecondaryMount->SetGuidingEnabled(false);
    }

    m_guideOutputDisabled = true;

    startStr = wxDateTime::Now().FormatISOCombined(' ');
    m_measuring = true;
    m_startTime = ::wxGetUTCTimeMillis().GetValue();
    SetSizerAndFit(m_vSizer);
}

void GuidingAsstWin::DoStop(const wxString& status)
{
    m_measuring = false;
    m_recommendgrid->Show(true);
    m_dlgState = STATE_STOPPED;
    m_measurementsTaken = true;

    FillInstructions(m_dlgState);

    if (m_guideOutputDisabled)
    {
        Debug.Write(wxString::Format("GuidingAssistant: Re-enabling guide output (%d, %d)\n", m_savePrimaryMountEnabled, m_saveSecondaryMountEnabled));

        if (pMount)
            pMount->SetGuidingEnabled(m_savePrimaryMountEnabled);
        if (pSecondaryMount)
            pSecondaryMount->SetGuidingEnabled(m_saveSecondaryMountEnabled);

        m_guideOutputDisabled = false;
    }

    m_start->Enable(pFrame->pGuider->IsGuiding());
    m_stop->Enable(false);

    if (m_origSubFrames != -1)
    {
        pCamera->UseSubframes = m_origSubFrames ? true : false;
        m_origSubFrames = -1;
    }
}

void GuidingAsstWin::EndBacklashTest(bool completed)
{
    if (!completed)
    {
        m_backlashTool->StopMeasurement();
        m_othergrid->SetCellValue(m_backlash_loc, _("Backlash test aborted..."));
        m_graphBtn->Enable(m_backlashTool->IsGraphable());
    }

    m_measuringBacklash = false;
    m_backlashCB->Enable(true);
    Layout();
    GetSizer()->Fit(this);

    m_start->Enable(pFrame->pGuider->IsGuiding());
    m_stop->Enable(false);
    MakeRecommendations();
    if (!completed)
    {
        wxCommandEvent dummy;
        OnAppStateNotify(dummy);            // Make sure UI is in synch
    }
    DoStop();
}

void GuidingAsstWin::OnStop(wxCommandEvent& event)
{
    bool performBLT = m_backlashCB->IsChecked();
    bool longEnough;
    if (m_elapsedSecs < GA_MIN_SAMPLING_PERIOD && !m_measuringBacklash)
    {
        SampleWait waitDlg(GA_MIN_SAMPLING_PERIOD - m_elapsedSecs, performBLT);
        longEnough = (waitDlg.ShowModal() == wxOK);
    }
    else
        longEnough = true;

    m_gaStatus->SetLabel(wxEmptyString);
    if (longEnough && performBLT)
    {
        if (!m_measuringBacklash)                               // Run the backlash test after the sampling was completed
        {
            m_measuringBacklash = true;

            if (m_origSubFrames == -1)
                m_origSubFrames = pCamera->UseSubframes ? 1 : 0;
            pCamera->UseSubframes = false;

            m_gaStatus->SetLabelText(_("Measuring backlash... ") + m_backlashTool->GetLastStatus());
            Layout();
            GetSizer()->Fit(this);
            m_backlashCB->Enable(false);                        // Don't let user turn it off once we've started
            m_measuring = false;
            m_backlashTool->StartMeasurement(decDriftPerMin);
            m_instructions->SetLabel(_("Measuring backlash... "));
        }
        else
        {
            // User hit stop during bl test
            m_gaStatus->SetLabelText(wxEmptyString);
            MakeRecommendations();
            EndBacklashTest(false);
        }
    }
    else
    {
        if (longEnough)
            MakeRecommendations();
        DoStop();
    }
}

void GuidingAsstWin::OnAppStateNotify(wxCommandEvent& WXUNUSED(event))
{
    if (m_measuring || m_measuringBacklash)
    {
        if (!pFrame->pGuider->IsGuiding())
        {
            // if guiding stopped, stop measuring
            DoStop(_("Guiding stopped"));
        }
    }
    else
    {
        bool can_start = pFrame->pGuider->IsGuiding();
        m_start->Enable(can_start);
        if (can_start)
            m_dlgState = STATE_START_READY;
        else
            m_dlgState = STATE_NO_STAR;
        FillInstructions(m_dlgState);
    }
}

void GuidingAsstWin::OnClose(wxCloseEvent& evt)
{
    DoStop();

    // save the window position
    int x, y;
    GetPosition(&x, &y);
    pConfig->Global.SetInt("/GuidingAssistant/pos.x", x);
    pConfig->Global.SetInt("/GuidingAssistant/pos.y", y);

    Destroy();
}

void GuidingAsstWin::FillResultCell(wxGrid *pGrid, const wxGridCellCoords& loc, double pxVal, double asVal, const wxString& units1, const wxString& units2,
    const wxString& extraInfo)
{
    pGrid->SetCellValue(loc, wxString::Format("%6.2f %s (%6.2f %s %s)", pxVal, units1, asVal, units2, extraInfo));
}

void GuidingAsstWin::UpdateInfo(const GuideStepInfo& info)
{
    double ra = info.mountOffset.X;
    double dec = info.mountOffset.Y;
    // Update the time measures
    wxLongLong_t elapsedms = ::wxGetUTCTimeMillis().GetValue() - m_startTime;
    m_elapsedSecs = (double)elapsedms / 1000.0;
    // add offset info to various stats accumulations
    m_hpfRAStats.AddValue(m_raHPF.AddValue(ra));
    double prevRAlpf = m_raLPF.GetCurrentLPF();
    double newRAlpf = m_raLPF.AddValue(ra);
    if (m_lpfRAStats.GetCount() == 0)
        prevRAlpf = newRAlpf;
    m_lpfRAStats.AddValue(newRAlpf);
    m_hpfDecStats.AddValue(m_decHPF.AddValue(dec));
    if (m_decAxisStats.GetCount() == 0)
        m_axisTimebase = wxGetCurrentTime();
    m_decAxisStats.AddGuideInfo(wxGetCurrentTime() - m_axisTimebase, dec, 0);
    m_raAxisStats.AddGuideInfo(wxGetCurrentTime() - m_axisTimebase, ra, 0);

    // Compute the maximum interval RA movement rate using low-passed-filtered data
    if (m_lpfRAStats.GetCount() == 1)
    {
        m_startPos = info.mountOffset;
        maxRateRA = 0.0;
    }
    else
    {
        double dt = info.time - m_lastTime;
        if (dt > 0.0001)
        {
            double raRate = fabs(newRAlpf - prevRAlpf) / dt;
            if (raRate > maxRateRA)
                maxRateRA = raRate;
        }
    }

    m_lastTime = info.time;
    sumSNR += info.starSNR;
    sumMass += info.starMass;
    double n = (double)m_lpfRAStats.GetCount();

    wxString SEC(_("s"));
    wxString MSEC(_("ms"));
    wxString PX(_("px"));
    wxString ARCSEC(_("arc-sec"));
    wxString ARCMIN(_("arc-min"));
    wxString PXPERMIN(_("px/min"));
    wxString PXPERSEC(_("px/sec"));
    wxString ARCSECPERMIN(_("arc-sec/min"));
    wxString ARCSECPERSEC(_("arc-sec/sec"));

    m_statusgrid->SetCellValue(m_timestamp_loc, startStr);
    m_statusgrid->SetCellValue(m_exposuretime_loc, wxString::Format("%g%s", (double)pFrame->RequestedExposureDuration() / 1000.0, SEC));
    m_statusgrid->SetCellValue(m_snr_loc, wxString::Format("%.1f", sumSNR / n));
    m_statusgrid->SetCellValue(m_starmass_loc, wxString::Format("%.1f", sumMass / n));
    m_statusgrid->SetCellValue(m_elapsedtime_loc, wxString::Format("%u%s", (unsigned int)(elapsedms / 1000), SEC));
    m_statusgrid->SetCellValue(m_samplecount_loc, wxString::Format("%.0f", n));

    if (n > 1)
    {
        // Update the realtime high-frequency stats
        double rarms = m_hpfRAStats.GetSigma();
        double decrms = m_hpfDecStats.GetSigma();
        double combined = hypot(rarms, decrms);

        // Update the running estimate of polar alignment error using linear-fit dec drift rate
        double pxscale = pFrame->GetCameraPixelScale();
        double declination = pPointingSource->GetDeclination();
        double cosdec;
        if (declination == UNKNOWN_DECLINATION)
            cosdec = 1.0; // assume declination 0
        else
            cosdec = cos(declination);
        // polar alignment error from Barrett:
        // http://celestialwonders.com/articles/polaralignment/PolarAlignmentAccuracy.pdf
        double intcpt;
        m_decAxisStats.GetLinearFitResults(&decDriftPerMin, &intcpt);
        decDriftPerMin = 60.0 * decDriftPerMin;
        alignmentError = 3.8197 * fabs(decDriftPerMin) * pxscale / cosdec;

        // update grid display w/ running stats
        FillResultCell(m_displacementgrid, m_ra_rms_loc, rarms, rarms * pxscale, PX, ARCSEC);
        FillResultCell(m_displacementgrid, m_dec_rms_loc, decrms, decrms * pxscale, PX, ARCSEC);
        FillResultCell(m_displacementgrid, m_total_rms_loc, combined, combined * pxscale, PX, ARCSEC);
        FillResultCell(m_othergrid, m_ra_peak_loc, 
            m_raAxisStats.GetMaxDelta(), m_raAxisStats.GetMaxDelta() * pxscale, PX, ARCSEC);
        FillResultCell(m_othergrid, m_dec_peak_loc,
            m_decAxisStats.GetMaxDelta(), m_decAxisStats.GetMaxDelta() * pxscale, PX, ARCSEC);
        double raPkPk = m_lpfRAStats.GetMaximum() - m_lpfRAStats.GetMinimum();
        FillResultCell(m_othergrid, m_ra_peakpeak_loc, raPkPk, raPkPk * pxscale, PX, ARCSEC);
        double raDriftRate = (ra - m_startPos.X) / m_elapsedSecs * 60.0;            // Raw max-min, can't smooth this one reliably
        FillResultCell(m_othergrid, m_ra_drift_loc, raDriftRate, raDriftRate * pxscale, PXPERMIN, ARCSECPERMIN);
        FillResultCell(m_othergrid, m_ra_peak_drift_loc, maxRateRA, maxRateRA * pxscale, PXPERSEC, ARCSECPERSEC);
        m_othergrid->SetCellValue(m_ra_drift_exp_loc, maxRateRA <= 0.0 ? _(" ") :
            wxString::Format("%6.1f %s ", 1.3 * rarms / maxRateRA, SEC));              // Will get revised when min-move is computed
        FillResultCell(m_othergrid, m_dec_drift_loc, decDriftPerMin, decDriftPerMin * pxscale, PXPERMIN, ARCSECPERMIN);
        m_othergrid->SetCellValue(m_pae_loc, wxString::Format("%s %.1f %s", declination == UNKNOWN_DECLINATION ? "> " : "", alignmentError, ARCMIN));
    }
}

wxWindow *GuidingAssistant::CreateDialogBox()
{
    return new GuidingAsstWin();
}

void GuidingAssistant::NotifyGuideStep(const GuideStepInfo& info)
{
    if (pFrame && pFrame->pGuidingAssistant)
    {
        GuidingAsstWin *win = static_cast<GuidingAsstWin *>(pFrame->pGuidingAssistant);
        if (win->m_measuring)
            win->UpdateInfo(info);
    }
}

void GuidingAssistant::NotifyFrameDropped(const FrameDroppedInfo& info)
{
    if (pFrame && pFrame->pGuidingAssistant)
    {
        // anything needed?
    }
}

void GuidingAssistant::NotifyBacklashStep(const PHD_Point& camLoc)
{
    if (pFrame && pFrame->pGuidingAssistant)
    {
        GuidingAsstWin *win = static_cast<GuidingAsstWin *>(pFrame->pGuidingAssistant);
        if (win->m_measuringBacklash)
            win->BacklashStep(camLoc);
    }
}

void GuidingAssistant::NotifyBacklashError()
{
    if (pFrame && pFrame->pGuidingAssistant)
    {
        GuidingAsstWin *win = static_cast<GuidingAsstWin *>(pFrame->pGuidingAssistant);
        if (win->m_measuringBacklash)
            win->BacklashError();
    }
}

void GuidingAssistant::UpdateUIControls()
{
    // notify GuidingAssistant window to update its controls
    if (pFrame && pFrame->pGuidingAssistant)
    {
        wxCommandEvent event(APPSTATE_NOTIFY_EVENT, pFrame->GetId());
        event.SetEventObject(pFrame);
        wxPostEvent(pFrame->pGuidingAssistant, event);
    }
}
