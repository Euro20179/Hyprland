#include "Events.hpp"

#include "../Compositor.hpp"
#include "../helpers/WLClasses.hpp"
#include "../managers/InputManager.hpp"
#include "../render/Renderer.hpp"

// ------------------------------------------------------------ //
//  __          _______ _   _ _____   ______          _______   //
//  \ \        / /_   _| \ | |  __ \ / __ \ \        / / ____|  //
//   \ \  /\  / /  | | |  \| | |  | | |  | \ \  /\  / / (___    //
//    \ \/  \/ /   | | | . ` | |  | | |  | |\ \/  \/ / \___ \   //
//     \  /\  /   _| |_| |\  | |__| | |__| | \  /\  /  ____) |  //
//      \/  \/   |_____|_| \_|_____/ \____/   \/  \/  |_____/   //
//                                                              //
// ------------------------------------------------------------ //

void Events::listener_mapWindow(wl_listener* listener, void* data) {
    CWindow* PWINDOW = wl_container_of(listener, PWINDOW, listen_mapWindow);

    const auto PMONITOR = g_pCompositor->getMonitorFromCursor();
    PWINDOW->m_iMonitorID = PMONITOR->ID;
    PWINDOW->m_bMappedX11 = true;
    PWINDOW->m_iWorkspaceID = PMONITOR->activeWorkspace;
    PWINDOW->m_bIsMapped = true;

    // checks if the window wants borders and sets the appriopriate flag
    g_pXWaylandManager->checkBorders(PWINDOW);

    const auto PWINDOWSURFACE = g_pXWaylandManager->getWindowSurface(PWINDOW);

    if (!PWINDOWSURFACE) {
        g_pCompositor->m_lWindows.remove(*PWINDOW);
        return;
    }

    wl_signal_add(&PWINDOWSURFACE->events.new_subsurface, &PWINDOW->listen_newSubsurfaceWindow);

    if (g_pXWaylandManager->shouldBeFloated(PWINDOW))
        PWINDOW->m_bIsFloating = true;

    // window rules
    const auto WINDOWRULES = g_pConfigManager->getMatchingRules(PWINDOW);

    for (auto& r : WINDOWRULES) {
        if (r.szRule.find("monitor") == 0) {
            try {
                const long int MONITOR = std::stoi(r.szRule.substr(r.szRule.find(" ")));

                if (MONITOR >= (long int)g_pCompositor->m_lMonitors.size() || MONITOR < (long int)0)
                    PWINDOW->m_iMonitorID = 0;
                else
                    PWINDOW->m_iMonitorID = MONITOR;

                PWINDOW->m_iWorkspaceID = g_pCompositor->getMonitorFromID(PWINDOW->m_iMonitorID)->activeWorkspace;

                Debug::log(LOG, "Rule monitor, applying to window %x -> mon: %i, workspace: %i", PWINDOW, PWINDOW->m_iMonitorID, PWINDOW->m_iWorkspaceID);
            } catch (...) {
                Debug::log(LOG, "Rule monitor failed, rule: %s -> %s", r.szRule.c_str(), r.szValue.c_str());
            }
        } else if (r.szRule.find("float") == 0) {
            PWINDOW->m_bIsFloating = true;
        } else if (r.szRule.find("tile") == 0) {
            PWINDOW->m_bIsFloating = false;
        }
    }

    if (PWINDOW->m_bIsFloating) {
        g_pLayoutManager->getCurrentLayout()->onWindowCreatedFloating(PWINDOW);

        // size and move rules
        for (auto& r : WINDOWRULES) {
            if (r.szRule.find("size") == 0) {
                try {
                    const auto VALUE = r.szRule.substr(r.szRule.find(" ") + 1);
                    const auto SIZEX = stoi(VALUE.substr(0, VALUE.find(" ")));
                    const auto SIZEY = stoi(VALUE.substr(VALUE.find(" ") + 1));

                    Debug::log(LOG, "Rule size, applying to window %x", PWINDOW);

                    PWINDOW->m_vEffectiveSize = Vector2D(SIZEX, SIZEY);
                    g_pXWaylandManager->setWindowSize(PWINDOW, PWINDOW->m_vEffectiveSize);
                } catch (...) {
                    Debug::log(LOG, "Rule size failed, rule: %s -> %s", r.szRule.c_str(), r.szValue.c_str());
                }
            } else if (r.szRule.find("move") == 0) {
                try {
                    const auto VALUE = r.szRule.substr(r.szRule.find(" ") + 1);
                    const auto POSX = stoi(VALUE.substr(0, VALUE.find(" ")));
                    const auto POSY = stoi(VALUE.substr(VALUE.find(" ") + 1));

                    Debug::log(LOG, "Rule move, applying to window %x", PWINDOW);

                    PWINDOW->m_vEffectivePosition = Vector2D(POSX, POSY) + PMONITOR->vecPosition;
                } catch (...) {
                    Debug::log(LOG, "Rule move failed, rule: %s -> %s", r.szRule.c_str(), r.szValue.c_str());
                }
            }
        }
    }
    else
        g_pLayoutManager->getCurrentLayout()->onWindowCreated(PWINDOW);

    PWINDOW->m_szTitle = g_pXWaylandManager->getTitle(PWINDOW);

    if (!PWINDOW->m_bIsModal)
        g_pCompositor->focusWindow(PWINDOW);

    Debug::log(LOG, "Map request dispatched, monitor %s, xywh: %f %f %f %f", PMONITOR->szName.c_str(), PWINDOW->m_vEffectivePosition.x, PWINDOW->m_vEffectivePosition.y, PWINDOW->m_vEffectiveSize.x, PWINDOW->m_vEffectiveSize.y);
}

void Events::listener_unmapWindow(wl_listener* listener, void* data) {
    CWindow* PWINDOW = wl_container_of(listener, PWINDOW, listen_unmapWindow);

    if (g_pXWaylandManager->getWindowSurface(PWINDOW) == g_pCompositor->m_pLastFocus)
        g_pCompositor->m_pLastFocus = nullptr;

    PWINDOW->m_bMappedX11 = false;

    // remove the fullscreen window status from workspace if we closed it
    const auto PWORKSPACE = g_pCompositor->getWorkspaceByID(PWINDOW->m_iWorkspaceID);

    if (PWORKSPACE->hasFullscreenWindow && PWINDOW->m_bIsFullscreen)
        PWORKSPACE->hasFullscreenWindow = false;

    g_pLayoutManager->getCurrentLayout()->onWindowRemoved(PWINDOW);

    // do this after onWindowRemoved because otherwise it'll think the window is invalid
    PWINDOW->m_bIsMapped = false;

    // refocus on a new window
    // TODO: investigate.
    // If a parent window has focus, any popups (XWayland) will be broken (they will disappear instantly)
    // This might be way more sinister in nature and have a problem more deeply
    // rooted in the code.
    // g_pInputManager->refocus();

    Debug::log(LOG, "Window %x unmapped", PWINDOW);
}

void Events::listener_commitWindow(wl_listener* listener, void* data) {
    CWindow* PWINDOW = wl_container_of(listener, PWINDOW, listen_commitWindow);

   // Debug::log(LOG, "Window %x committed", PWINDOW); // SPAM!
}

void Events::listener_destroyWindow(wl_listener* listener, void* data) {
    CWindow* PWINDOW = wl_container_of(listener, PWINDOW, listen_destroyWindow);

    if (g_pXWaylandManager->getWindowSurface(PWINDOW) == g_pCompositor->m_pLastFocus)
        g_pCompositor->m_pLastFocus = nullptr;

    // TODO:
    // We sometimes get invalid pointers here (stack ones)
    // this obviously means we tainted wlroots.
    // Investigate

    g_pLayoutManager->getCurrentLayout()->onWindowRemoved(PWINDOW);

    Debug::log(LOG, "Window %x destroyed", PWINDOW);

    wl_list_remove(&PWINDOW->listen_mapWindow.link);
    wl_list_remove(&PWINDOW->listen_unmapWindow.link);
    wl_list_remove(&PWINDOW->listen_destroyWindow.link);
    wl_list_remove(&PWINDOW->listen_setTitleWindow.link);
    wl_list_remove(&PWINDOW->listen_fullscreenWindow.link);
    if (PWINDOW->m_bIsX11) {
        wl_list_remove(&PWINDOW->listen_activateX11.link);
        wl_list_remove(&PWINDOW->listen_configureX11.link);
    } else {
        wl_list_remove(&PWINDOW->listen_commitWindow.link);
    }

    g_pCompositor->removeWindowFromVectorSafe(PWINDOW);
}

void Events::listener_setTitleWindow(wl_listener* listener, void* data) {
    CWindow* PWINDOW = wl_container_of(listener, PWINDOW, listen_setTitleWindow);

    if (!g_pCompositor->windowValidMapped(PWINDOW))
	    return;

    PWINDOW->m_szTitle = g_pXWaylandManager->getTitle(PWINDOW);

    Debug::log(LOG, "Window %x set title to %s", PWINDOW, PWINDOW->m_szTitle.c_str());
}

void Events::listener_fullscreenWindow(wl_listener* listener, void* data) {
    CWindow* PWINDOW = wl_container_of(listener, PWINDOW, listen_fullscreenWindow);

    g_pLayoutManager->getCurrentLayout()->fullscreenRequestForWindow(PWINDOW);

    Debug::log(LOG, "Window %x fullscreen to %i", PWINDOW, PWINDOW->m_bIsFullscreen);
}

void Events::listener_activate(wl_listener* listener, void* data) {
    // TODO
}

void Events::listener_activateX11(wl_listener* listener, void* data) {
    CWindow* PWINDOW = wl_container_of(listener, PWINDOW, listen_activateX11);

    if (PWINDOW->m_iX11Type == 1 /* Managed */) {
        wlr_xwayland_surface_activate(PWINDOW->m_uSurface.xwayland, 1);
    }
}

void Events::listener_configureX11(wl_listener* listener, void* data) {
    CWindow* PWINDOW = wl_container_of(listener, PWINDOW, listen_configureX11);

    const auto E = (wlr_xwayland_surface_configure_event*)data;

    // TODO: ignore if tiled?
    wlr_xwayland_surface_configure(PWINDOW->m_uSurface.xwayland, E->x, E->y, E->width, E->height);
}

void Events::listener_surfaceXWayland(wl_listener* listener, void* data) {
    const auto XWSURFACE = (wlr_xwayland_surface*)data;

    g_pCompositor->m_lWindows.push_back(CWindow());
    const auto PNEWWINDOW = &g_pCompositor->m_lWindows.back();

    PNEWWINDOW->m_uSurface.xwayland = XWSURFACE;
    PNEWWINDOW->m_iX11Type = XWSURFACE->override_redirect ? 2 : 1;
    PNEWWINDOW->m_bIsX11 = true;

    wl_signal_add(&XWSURFACE->events.map, &PNEWWINDOW->listen_mapWindow);
    wl_signal_add(&XWSURFACE->events.unmap, &PNEWWINDOW->listen_unmapWindow);
    wl_signal_add(&XWSURFACE->events.request_activate, &PNEWWINDOW->listen_activateX11);
    wl_signal_add(&XWSURFACE->events.request_configure, &PNEWWINDOW->listen_configureX11);
    wl_signal_add(&XWSURFACE->events.set_title, &PNEWWINDOW->listen_setTitleWindow);
    wl_signal_add(&XWSURFACE->events.destroy, &PNEWWINDOW->listen_destroyWindow);
    wl_signal_add(&XWSURFACE->events.request_fullscreen, &PNEWWINDOW->listen_fullscreenWindow);

    Debug::log(LOG, "New XWayland Surface created.");
}

void Events::listener_newXDGSurface(wl_listener* listener, void* data) {
    // A window got opened
    const auto XDGSURFACE = (wlr_xdg_surface*)data;

    if (XDGSURFACE->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL)
        return;  // TODO: handle?

    g_pCompositor->m_lWindows.push_back(CWindow());
    const auto PNEWWINDOW = &g_pCompositor->m_lWindows.back();
    PNEWWINDOW->m_uSurface.xdg = XDGSURFACE;

    wl_signal_add(&XDGSURFACE->surface->events.commit, &PNEWWINDOW->listen_commitWindow);
    wl_signal_add(&XDGSURFACE->events.map, &PNEWWINDOW->listen_mapWindow);
    wl_signal_add(&XDGSURFACE->events.unmap, &PNEWWINDOW->listen_unmapWindow);
    wl_signal_add(&XDGSURFACE->events.destroy, &PNEWWINDOW->listen_destroyWindow);
    wl_signal_add(&XDGSURFACE->toplevel->events.set_title, &PNEWWINDOW->listen_setTitleWindow);
    wl_signal_add(&XDGSURFACE->toplevel->events.request_fullscreen, &PNEWWINDOW->listen_fullscreenWindow);

    Debug::log(LOG, "New XDG Surface created.");
}

//
// Subsurfaces
//

void createSubsurface(CWindow* pWindow, wlr_subsurface* pSubsurface) {
    if (!pWindow || !pSubsurface)
        return;

    g_pCompositor->m_lSubsurfaces.push_back(SSubsurface());
    const auto PNEWSUBSURFACE = &g_pCompositor->m_lSubsurfaces.back();

    wl_signal_add(&pSubsurface->events.destroy, &PNEWSUBSURFACE->listen_destroySubsurface);
    wl_signal_add(&pSubsurface->events.map, &PNEWSUBSURFACE->listen_mapSubsurface);
    wl_signal_add(&pSubsurface->events.unmap, &PNEWSUBSURFACE->listen_unmapSubsurface);
    wl_signal_add(&pSubsurface->surface->events.commit, &PNEWSUBSURFACE->listen_commitSubsurface);
}

void Events::listener_newSubsurfaceWindow(wl_listener* listener, void* data) {
    CWindow* PWINDOW = wl_container_of(listener, PWINDOW, listen_newSubsurfaceWindow);

    const auto PSUBSURFACE = (wlr_subsurface*)data;

    createSubsurface(PWINDOW, PSUBSURFACE);
}
