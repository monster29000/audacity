/**********************************************************************

  Audacity: A Digital Audio Editor

  ExportFileDialog.h
 
  Vitaly Sverchinsky

*******************************************************************/
#pragma once

#include "wxPanelWrapper.h"
#include <wx/event.h>

class wxSimplebook;
class wxFileCtrlEvent;

class Exporter;

class ExportOptionsHandler;

class AUDACITY_DLL_API ExportFileDialog final : public FileDialogWrapper
{
   ExportFileDialog(wxWindow* parent,
                    Exporter& exporter,
                    const wxString& defaultDir = {},
                    const wxString& defaultName = {},
                    const wxString& defaultFormatName = {},
                    const TranslatableString& title = XO("Export Audio"),
                    const wxPoint& pos = wxDefaultPosition,
                    const wxSize& sz = wxDefaultSize,
                    const wxString& name = wxFileDialogNameStr);
   
public:

   ~ExportFileDialog() override;

   ///\brief Shows export file dialog and configures exporter according to user selection and
   ///handles input errors/inconsistencies, which isn't possible with regular `wxDialog::Show`
   ///or `wxDialog::ShowModal`
   static int RunModal(wxWindow* parent,
                       Exporter& exporter,
                       const wxString& defaultFilename = {},
                       const wxString& defaultFormatName = {},
                       const TranslatableString& title = XO("Export Audio"),
                       const wxPoint& pos = wxDefaultPosition,
                       const wxSize& sz = wxDefaultSize,
                       const wxString& name = wxFileDialogNameStr);
   
   void OnExtensionChanged(wxCommandEvent &evt);
   void OnHelp(wxCommandEvent &evt);
   
private:

   void OnFilterChanged(wxFileCtrlEvent & evt);
   
   void CreateExportOptions(wxWindow* exportOptionsPane);
   
   static void CreateUserPaneCallback(wxWindow *parent, wxUIntPtr userdata);
   
   Exporter& mExporter;
   
   wxSimplebook *mBook {nullptr};
   
   std::vector<std::unique_ptr<ExportOptionsHandler>> mOptionsHandlers;

   DECLARE_EVENT_TABLE()
};

extern AUDACITY_DLL_API StringSetting DefaultExportFormat;
