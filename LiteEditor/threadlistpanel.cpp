//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//
// copyright            : (C) 2008 by Eran Ifrah
// file name            : threadlistpanel.cpp
//
// -------------------------------------------------------------------------
// A
//              _____           _      _     _ _
//             /  __ \         | |    | |   (_) |
//             | /  \/ ___   __| | ___| |    _| |_ ___
//             | |    / _ \ / _  |/ _ \ |   | | __/ _ )
//             | \__/\ (_) | (_| |  __/ |___| | ||  __/
//              \____/\___/ \__,_|\___\_____/_|\__\___|
//
//                                                  F i l e
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; either version 2 of the License, or
//    (at your option) any later version.
//
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
#include "threadlistpanel.h"
#include "globals.h"
#include "manager.h"
#include <wx/wupdlock.h>

ThreadListPanel::ThreadListPanel(wxWindow* parent)
    : ThreadListBasePanel(parent)
{
}

ThreadListPanel::~ThreadListPanel() {}

void ThreadListPanel::OnItemActivated(wxDataViewEvent& event)
{
    if(!event.GetItem().IsOk()) return;

    wxVariant v;
    long threadId;
    m_dvListCtrl->GetValue(v, m_dvListCtrl->ItemToRow(event.GetItem()), 0);
    wxString str_id = v.GetString();
    if(str_id.ToCLong(&threadId)) {
        Manager* mgr = ManagerST::Get();
        mgr->DbgSetThread(threadId);
    }
}

void ThreadListPanel::PopulateList(const ThreadEntryArray& threads)
{
    // Check if the new thread list is the same as the current one
    if(IsTheSame(m_threads, threads)) {
        // No need to repopulate the list, just set the active thread indicator
        for(size_t i = 0; i < m_threads.size(); ++i) {
            if(m_threads.at(i).active) {
                wxVariant v = "NO";
                m_dvListCtrl->SetValue(v, i, 1);
            }
        }

        // Replace the current thread stack with the new one
        m_threads.clear();
        m_threads.insert(m_threads.end(), threads.begin(), threads.end());

        // Update the new active thread
        for(size_t i = 0; i < m_threads.size(); ++i) {
            if(m_threads.at(i).active) {
                wxVariant v = "YES";
                m_dvListCtrl->SetValue(v, i, 1);
            }
        }

    } else {
        wxWindowUpdateLocker locker(m_dvListCtrl);
        m_dvListCtrl->DeleteAllItems();

        // Replace the thread list
        m_threads.clear();
        m_threads.insert(m_threads.end(), threads.begin(), threads.end());

        int sel = wxNOT_FOUND;
        if(m_threads.empty()) return;

        for(size_t i = (m_threads.size() - 1); i >= 0; --i) {
            const ThreadEntry& entry = m_threads.at(i);

            wxString str_id;
            wxString str_active;

            str_id << entry.dbgid;
            str_active = entry.active ? "YES" : "NO";
            if(entry.active) {
                sel = i;
            }

            wxVector<wxVariant> cols;
            cols.push_back(str_id);
            cols.push_back(str_active);
            cols.push_back(entry.function);
            cols.push_back(entry.file);
            cols.push_back(entry.line);
            m_dvListCtrl->AppendItem(cols);
        }

        // Ensure that the active thread is visible
        if(sel != wxNOT_FOUND) {
            wxDataViewItem item = m_dvListCtrl->RowToItem(sel);
            m_dvListCtrl->EnsureVisible(item, 0);
        }
    }
}

void ThreadListPanel::Clear() { m_dvListCtrl->DeleteAllItems(); }

bool ThreadListPanel::IsTheSame(const ThreadEntryArray& threads1, const ThreadEntryArray& threads2)
{
    if(threads1.size() != threads2.size()) {
        return false;
    }

    for(size_t i = 0; i < threads1.size(); ++i) {
        const ThreadEntry& entry1 = threads1.at(i);
        const ThreadEntry& entry2 = threads2.at(i);
        if((entry1.file != entry2.file) || (entry1.function != entry2.function) || (entry1.line != entry2.line)) {
            return false;
        }
    }
    return true;
}
