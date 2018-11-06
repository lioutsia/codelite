#include "NodeFileManager.h"
#include "NodeDebugger.h"
#include "NodeJSDebuggerDlg.h"
#include "NodeJSEvents.h"
#include "NoteJSWorkspace.h"
#include "SocketAPI/clSocketBase.h"
#include "SocketAPI/clWebSocketClient.h"
#include "clConsoleBase.h"
#include "codelite_events.h"
#include "event_notifier.h"
#include "file_logger.h"
#include "globals.h"
#include "ieditor.h"
#include "imanager.h"
#include "macros.h"
#include "processreaderthread.h"
#include <wx/msgdlg.h>

#define NODE_CLI_DEBUGGER_NAME "Node.js - CLI"

#define CHECK_RUNNING() \
    if(!IsRunning()) { return; }

#define CHECK_SHOULD_HANDLE(evt)                      \
    evt.Skip();                                       \
    if(!IsRunning()) { return; }                      \
    if(!NodeJSWorkspace::Get()->IsOpen()) { return; } \
    evt.Skip(false);

NodeDebugger::NodeDebugger()
    : m_socket(this)
{
    EventNotifier::Get()->Bind(wxEVT_DBG_UI_START, &NodeDebugger::OnDebugStart, this);
    EventNotifier::Get()->Bind(wxEVT_DBG_UI_CONTINUE, &NodeDebugger::OnDebugContinue, this);
    EventNotifier::Get()->Bind(wxEVT_DBG_UI_STOP, &NodeDebugger::OnStopDebugger, this);
    EventNotifier::Get()->Bind(wxEVT_DBG_UI_NEXT, &NodeDebugger::OnDebugNext, this);
    EventNotifier::Get()->Bind(wxEVT_DBG_IS_RUNNING, &NodeDebugger::OnDebugIsRunning, this);
    EventNotifier::Get()->Bind(wxEVT_DBG_UI_TOGGLE_BREAKPOINT, &NodeDebugger::OnToggleBreakpoint, this);
    EventNotifier::Get()->Bind(wxEVT_WORKSPACE_CLOSED, &NodeDebugger::OnWorkspaceClosed, this);

    EventNotifier::Get()->Bind(wxEVT_NODEJS_DEBUGGER_INTERACT, &NodeDebugger::OnInteract, this);

    Bind(wxEVT_ASYNC_PROCESS_OUTPUT, &NodeDebugger::OnProcessOutput, this);
    Bind(wxEVT_ASYNC_PROCESS_TERMINATED, &NodeDebugger::OnProcessTerminated, this);

    Bind(wxEVT_WEBSOCKET_CONNECTED, &NodeDebugger::OnWebSocketConnected, this);
    Bind(wxEVT_WEBSOCKET_ERROR, &NodeDebugger::OnWebSocketError, this);
    Bind(wxEVT_WEBSOCKET_ONMESSAGE, &NodeDebugger::OnWebSocketOnMessage, this);
    Bind(wxEVT_WEBSOCKET_DISCONNECTED, &NodeDebugger::OnWebSocketDisconnected, this);
}

NodeDebugger::~NodeDebugger()
{
    EventNotifier::Get()->Unbind(wxEVT_DBG_UI_START, &NodeDebugger::OnDebugStart, this);
    EventNotifier::Get()->Unbind(wxEVT_DBG_UI_CONTINUE, &NodeDebugger::OnDebugContinue, this);
    EventNotifier::Get()->Unbind(wxEVT_DBG_UI_STOP, &NodeDebugger::OnStopDebugger, this);
    EventNotifier::Get()->Unbind(wxEVT_DBG_UI_NEXT, &NodeDebugger::OnDebugNext, this);
    EventNotifier::Get()->Unbind(wxEVT_DBG_IS_RUNNING, &NodeDebugger::OnDebugIsRunning, this);
    EventNotifier::Get()->Unbind(wxEVT_DBG_UI_TOGGLE_BREAKPOINT, &NodeDebugger::OnToggleBreakpoint, this);
    EventNotifier::Get()->Unbind(wxEVT_WORKSPACE_CLOSED, &NodeDebugger::OnWorkspaceClosed, this);

    EventNotifier::Get()->Unbind(wxEVT_NODEJS_DEBUGGER_INTERACT, &NodeDebugger::OnInteract, this);

    Unbind(wxEVT_ASYNC_PROCESS_OUTPUT, &NodeDebugger::OnProcessOutput, this);
    Unbind(wxEVT_ASYNC_PROCESS_TERMINATED, &NodeDebugger::OnProcessTerminated, this);
    Unbind(wxEVT_WEBSOCKET_CONNECTED, &NodeDebugger::OnWebSocketConnected, this);
    Unbind(wxEVT_WEBSOCKET_ERROR, &NodeDebugger::OnWebSocketError, this);
    Unbind(wxEVT_WEBSOCKET_ONMESSAGE, &NodeDebugger::OnWebSocketOnMessage, this);
}

void NodeDebugger::OnDebugStart(clDebugEvent& event)
{
    event.Skip();
    CHECK_COND_RET(NodeJSWorkspace::Get()->IsOpen());

    event.SetFeatures(0); // No special features by the NodeJS debugger

    // Ours to handle
    event.Skip(false);

    if(!IsRunning()) {
        NodeJSDebuggerDlg dlg(EventNotifier::Get()->TopFrame(), NodeJSDebuggerDlg::kDebugCLI);
        if(dlg.ShowModal() != wxID_OK) { return; }

        wxString command;
        wxString command_args;
        dlg.GetCommand(command, command_args);
        StartDebugger(command, command_args, dlg.GetWorkingDirectory());

    } else {
        OnDebugContinue(event);
    }
}

void NodeDebugger::OnDebugContinue(clDebugEvent& event)
{
    CHECK_SHOULD_HANDLE(event);
    m_protocol.Continue(m_socket);
}

void NodeDebugger::OnStopDebugger(clDebugEvent& event)
{
    CHECK_SHOULD_HANDLE(event);

    // Terminate the process
    m_process->Terminate();
    m_socket.Close();
}

void NodeDebugger::OnDebugNext(clDebugEvent& event)
{
    CHECK_SHOULD_HANDLE(event);
    m_protocol.Next(m_socket);
}

bool NodeDebugger::IsRunning() const { return m_process != NULL; }

void NodeDebugger::DoCleanup()
{
    clDEBUG() << "Cleaning Nodejs debugger...";
    m_canInteract = false;
    m_workingDirectory.Clear();
    if(m_process) { m_process->Terminate(); }
    m_socket.Close();
    NodeFileManager::Get().Clear();
}

void NodeDebugger::OnDebugIsRunning(clDebugEvent& event)
{
    if(IsRunning()) {
        event.SetAnswer(true);

    } else {
        event.Skip();
    }
}

void NodeDebugger::OnProcessTerminated(clProcessEvent& event)
{
    clDEBUG() << "Nodejs process terminated";
    wxUnusedVar(event);
    wxDELETE(m_process);

    clDebugEvent e(wxEVT_NODEJS_CLI_DEBUGGER_STOPPED);
    e.SetDebuggerName(NODE_CLI_DEBUGGER_NAME);
    EventNotifier::Get()->AddPendingEvent(e);
    DoCleanup();
}

void NodeDebugger::OnProcessOutput(clProcessEvent& event)
{
    clDEBUG1() << event.GetOutput();
    const wxString& processOutput = event.GetOutput();
    int where = processOutput.Find("ws://");
    if(where != wxNOT_FOUND) {
        wxString websocketAddress = processOutput.Mid(where);
        websocketAddress = websocketAddress.BeforeFirst('\n');
        websocketAddress.Trim().Trim(false);

        try {
            clDEBUG() << "Attempting to connect debugger on" << websocketAddress;

            // Initialise the socket
            m_socket.Initialise();
            // Start a helper thread to listen on the this socket
            m_socket.StartLoop(websocketAddress);
        } catch(clSocketException& e) {
            clWARNING() << e.what();
        }
    }
}

void NodeDebugger::StartDebugger(const wxString& command, const wxString& command_args,
                                      const wxString& workingDirectory)
{
#if 0
    if(::wxMessageBox(_("The old debugger protocol is not supported by your current Node.js version.\nWould you "
                        "CodeLite to a CLI debug session for you in a terminal?"),
                      _("CodeLite"), wxICON_QUESTION | wxYES_NO | wxCANCEL | wxYES_DEFAULT,
                      EventNotifier::Get()->TopFrame()) == wxYES) {
        clConsoleBase::Ptr_t console = clConsoleBase::GetTerminal();
        console->SetWorkingDirectory(workingDirectory);
        console->SetCommand(command, command_args);
        console->SetTerminalNeeded(true);
        console->Start();
    }
#else
    if(m_socket.IsConnected()) {
        clDEBUG() << "An instance of the debugger is already running";
        return;
    }
    size_t createFlags = IProcessCreateDefault;
    wxString one_liner = command;
    if(!command_args.IsEmpty()) { one_liner << " " << command_args; }
    m_process = ::CreateAsyncProcess(this, one_liner, createFlags, workingDirectory);
    if(!m_process) {
        ::wxMessageBox(wxString() << _("Failed to launch NodeJS: ") << command);
        DoCleanup();
        return;
    }

    // Keep the working directory
    m_workingDirectory = workingDirectory;

    clDebugEvent eventStart(wxEVT_NODEJS_CLI_DEBUGGER_STARTED);
    eventStart.SetDebuggerName(NODE_CLI_DEBUGGER_NAME);
    EventNotifier::Get()->AddPendingEvent(eventStart);

    // Apply all breakpoints
    ApplyAllBerakpoints();

    // request the current backtrace
    Callstack();
#endif
}

bool NodeDebugger::IsCanInteract() const { return m_process && m_canInteract; }

void NodeDebugger::OnToggleBreakpoint(clDebugEvent& event)
{
    event.Skip();
    if(!NodeJSWorkspace::Get()->IsOpen()) { return; }
    event.Skip(false);
    IEditor* editor = clGetManager()->GetActiveEditor();
    if(IsRunning()) {
        if(editor && (editor->GetFileName().GetFullPath() == event.GetFileName())) {
            // Correct editor
            // add marker
            NodeJSBreakpoint bp = m_bptManager.GetBreakpoint(event.GetFileName(), event.GetInt());
            if(bp.IsOk()) {
                DeleteBreakpoint(bp);
            } else {
                SetBreakpoint(event.GetFileName(), event.GetInt());
            }
            // Update the UI
        }
    } else {

        NodeJSBreakpoint bp = m_bptManager.GetBreakpoint(event.GetFileName(), event.GetInt());
        if(bp.IsOk()) {
            m_bptManager.DeleteBreakpoint(event.GetFileName(), event.GetInt());
        } else {
            m_bptManager.AddBreakpoint(event.GetFileName(), event.GetInt());
        }
    }
    if(editor) { m_bptManager.SetBreakpoints(editor); }
}

void NodeDebugger::SetBreakpoint(const wxFileName& file, int lineNumber, bool useVoidHandler)
{
    // We have no breakpoint on this file/line (yet)
    m_bptManager.AddBreakpoint(file, lineNumber);
    const NodeJSBreakpoint& bp = m_bptManager.GetBreakpoint(file, lineNumber);
    if(!bp.IsOk()) { return; }

    // NodeJS debugger likes his file path in a certain format (relative, all backslashes escaped)
    wxString file_path = GetBpRelativeFilePath(bp);
}

void NodeDebugger::DeleteBreakpoint(const NodeJSBreakpoint& bp)
{
    if(!bp.IsOk()) { return; }
}

void NodeDebugger::ListBreakpoints() {}

wxString NodeDebugger::GetBpRelativeFilePath(const NodeJSBreakpoint& bp) const
{
    wxFileName fn(bp.GetFilename());
    fn.MakeRelativeTo(GetWorkingDirectory());
    wxString file_path = fn.GetFullPath();

    // We need to escapte backslashes, otherwise, it wont work...
    file_path.Replace("\\", "\\\\");
    return file_path;
}

void NodeDebugger::ApplyAllBerakpoints()
{
    const NodeJSBreakpoint::List_t& breakpoints = m_bptManager.GetBreakpoints();
    std::for_each(breakpoints.begin(), breakpoints.end(),
                  [&](const NodeJSBreakpoint& bp) { SetBreakpoint(bp.GetFilename(), bp.GetLine(), true); });
    // Now that we have applied all the breapoints, we can ask for list of the actual breakpoints from the debugger
    ListBreakpoints();
}

void NodeDebugger::OnWebSocketConnected(clCommandEvent& event)
{
    // Now that the connection has been established, we can send the startup commands
    m_protocol.SendStartCommands(m_socket);
}

void NodeDebugger::OnWebSocketError(clCommandEvent& event)
{
    // an error occured!, terminate the debug session
    if(m_process) { m_process->Terminate(); }
}

void NodeDebugger::OnWebSocketOnMessage(clCommandEvent& event)
{
    // We got a message from the websocket
    clDEBUG() << "<--" << event.GetString();
    m_protocol.ProcessMessage(event.GetString(), m_socket);
}

void NodeDebugger::OnWebSocketDisconnected(clCommandEvent& event) {}

void NodeDebugger::OnWorkspaceClosed(wxCommandEvent& event)
{
    event.Skip();
    DoCleanup();
}

void NodeDebugger::OnInteract(clDebugEvent& event)
{
    event.Skip();
    m_canInteract = event.IsAnswer();
}