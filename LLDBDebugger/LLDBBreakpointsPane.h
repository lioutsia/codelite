#ifndef LLDBBREAKPOINTSPANE_H
#define LLDBBREAKPOINTSPANE_H

#include "UI.h"
#include "LLDBEvent.h"
#include "LLDBBreakpoint.h"

class LLDBDebugger;
class LLDBDebuggerPlugin;
class LLDBBreakpointsPane : public LLDBBreakpointsPaneBase
{
    LLDBDebuggerPlugin *m_plugin;
    LLDBDebugger* m_lldb;
    
public:
    LLDBBreakpointsPane(wxWindow* parent, LLDBDebuggerPlugin *plugin);
    virtual ~LLDBBreakpointsPane();
    
    void Initialize();
    void Clear();
    
    // Event handlers
    void OnBreakpointsUpdated(LLDBEvent &event);
    LLDBBreakpoint* GetBreakpoint(const wxDataViewItem& item);
protected:
    void GotoBreakpoint(LLDBBreakpoint* bp);
    
protected:
    virtual void OnDeleteAll(wxCommandEvent& event);
    virtual void OnDeleteAllUI(wxUpdateUIEvent& event);
    virtual void OnDeleteBreakpoint(wxCommandEvent& event);
    virtual void OnDeleteBreakpointUI(wxUpdateUIEvent& event);
    virtual void OnNewBreakpoint(wxCommandEvent& event);
    virtual void OnNewBreakpointUI(wxUpdateUIEvent& event);
    virtual void OnBreakpointActivated(wxDataViewEvent& event);
};

#endif // LLDBBREAKPOINTSPANE_H
