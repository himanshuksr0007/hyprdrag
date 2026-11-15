#define WLR_USE_UNSTABLE

#include <unistd.h>

#include <hyprland/src/includes.hpp>
#include <hyprland/src/version.h>
#include <chrono>
#include <format>

#include <hyprland/src/ipc/s2/S2.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>

#include "globals.hpp"

// Do NOT change this function.
APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

// Listener handles for the drag controller's own signals. A listener stays
// registered while its handle lives, so these are file-static; PLUGIN_EXIT
// resets them, which unregisters.
static CHyprSignalListener dragMotionListener;
static CHyprSignalListener dragEndedListener;

// State tracking variables
static bool wasCurrentlyDragging = false;
static bool wasMoving = false;
static PHLWINDOW lastDraggedWindow = nullptr;

// Cached string constants
static constexpr const char* EVENT_DRAG_MOVE_START   = "hyprdrag-movestart";
static constexpr const char* EVENT_DRAG_MOVE_STOP    = "hyprdrag-movestop";
static constexpr const char* EVENT_DRAG_RESIZE_START = "hyprdrag-resizestart";
static constexpr const char* EVENT_DRAG_RESIZE_STOP  = "hyprdrag-resizestop";

static void handleDragEvent(PHLWINDOW window, bool isMoving, bool isStart) {
    if (!window) {
        return;
    }

    std::string address = std::format("0x{:x}", reinterpret_cast<uintptr_t>(window.get()));

    // [isStart][isMoving] indexing
    static constexpr const char* events[2][2] = {
        {EVENT_DRAG_RESIZE_STOP, EVENT_DRAG_MOVE_STOP},     // end events
        {EVENT_DRAG_RESIZE_START, EVENT_DRAG_MOVE_START}  // start events
    };

    // Send IPC event. (Up to Hyprland 0.56 this went through the event
    // manager; 0.57 posts straight to the socket2 implementation.)
    IPC::Socket2::sock()->postEvent({.event = events[isStart][isMoving], .data = address});

    // The window tags the original plugin set here (tag:hyprdrag-move /
    // tag:hyprdrag-resize, for windowrules) are gone: the tagwindow dispatch
    // they relied on no longer exists in 0.57.
}

static void handleDragEnd() {
    if (!lastDraggedWindow) {
        return;
    }

    handleDragEvent(lastDraggedWindow, wasMoving, false);
    lastDraggedWindow = nullptr;
}

// The drag state lives in the layout manager's drag controller since 0.57
// (it was the input manager's dragged window before). Its motion signal
// fires on mouse moves while a drag is running, so a begin is the first
// motion that finds a target, exactly how the original callback version
// detected it.
static void checkDragState() {
    const auto controller = g_layoutManager ? g_layoutManager->dragController() : nullptr;
    const auto target     = controller ? controller->target() : nullptr;
    const auto window     = target ? target->window() : nullptr;

    const bool isCurrentlyDragging = !!window;

    if (isCurrentlyDragging && !wasCurrentlyDragging) { // Drag started
        const auto mode = controller->mode();
        wasMoving       = mode == MBIND_MOVE;

        lastDraggedWindow = window;
        handleDragEvent(window, wasMoving, true);
    } else if (!isCurrentlyDragging && wasCurrentlyDragging) { // Drag ended without an end signal
        handleDragEnd();
    }

    wasCurrentlyDragging = isCurrentlyDragging;
}

static void onDragEnded() {
    if (wasCurrentlyDragging) {
        handleDragEnd();
        wasCurrentlyDragging = false;
    }
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string HASH = __hyprland_api_get_hash();
    const std::string CLIENTHASH = __hyprland_api_get_client_hash();

    if (HASH != CLIENTHASH) {
        throw std::runtime_error("[hyprdrag] Version mismatch");
    }

    if (g_layoutManager) {
        const auto& controller = g_layoutManager->dragController();
        dragMotionListener     = controller->m_events.motion.listen([] { checkDragState(); });
        dragEndedListener      = controller->m_events.ended.listen([] { onDragEnded(); });
    }

    return {"hyprdrag", "Window move/resize interaction plugin", "yz778", "0.2.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    dragMotionListener = {};
    dragEndedListener  = {};
    wasCurrentlyDragging = false;
    lastDraggedWindow = nullptr;
}
