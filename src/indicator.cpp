#include "indicator.hpp"

#include "sound.hpp"
#include "state.hpp"

#include <algorithm>
#include <gtk/gtk.h>
#include <libayatana-appindicator/app-indicator.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace cb {
namespace {

// The menu is built once and then only ever mutated. Replacing it - or changing
// its structure - makes the shell tear down whatever it is showing, which reads
// as a flicker if the user happens to have it open. Editing a label or an
// item's visibility instead travels as a dbusmenu property update, which the
// shell applies to the open menu in place.
constexpr size_t kMaxRows = 8;

struct Row {
    GtkWidget* item = nullptr;
    std::string label;
    bool visible = false;
};

struct App {
    AppIndicator* indicator = nullptr;
    GtkWidget* menu = nullptr;
    GtkWidget* header = nullptr;
    Row separator;                 // hidden while there are no sessions
    std::vector<Row> rows;
    GtkWidget* mute = nullptr;
    gulong muteHandler = 0;
    std::string headerLabel;
    Status shown = Status::Idle;
    bool iconSet = false;
    guint pending = 0;             // debounce source id
    bool debug = false;
};

void trace(const App* app, const std::string& what) {
    if (app->debug) g_printerr("claude-beep: %s\n", what.c_str());
}

bool isDir(const std::string& path) {
    struct stat st;
    return !path.empty() && stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// Icons ship next to the binary's install prefix; keep a few fallbacks so the
// indicator also runs straight out of the build tree.
std::string iconDir() {
    if (const char* env = getenv("CLAUDE_BEEP_ICONS"); env && isDir(env)) return env;

    char exe[4096];
    const ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (len > 0) {
        exe[len] = '\0';
        std::string dir(exe);
        dir = dir.substr(0, dir.find_last_of('/'));
        for (const char* rel : {"/../share/claude-beep/icons", "/../icons", "/icons"}) {
            const std::string candidate = dir + rel;
            if (isDir(candidate)) return candidate;
        }
    }

    const char* home = getenv("HOME");
    const std::string local = std::string(home ? home : "") + "/.local/share/claude-beep/icons";
    return isDir(local) ? local : std::string("icons");
}

const char* iconName(Status s) {
    switch (s) {
        case Status::Busy: return "claude-beep-busy";
        case Status::Waiting: return "claude-beep-wait";
        default: return "claude-beep-idle";
    }
}

const char* summary(Status s, size_t live) {
    switch (s) {
        case Status::Busy: return "Claude is working";
        case Status::Waiting: return "Claude needs you";
        default: return live ? "Claude is free" : "No Claude sessions";
    }
}

std::string basename(const std::string& path) {
    if (path.empty()) return "(unknown)";
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string rowLabel(const Session& s) {
    const char* dot = s.status == Status::Busy ? "● working"
                    : s.status == Status::Waiting ? "● asking"
                                                  : "● free";
    return std::string(dot) + "  —  " + basename(s.cwd);
}

// Both setters are no-ops when nothing changed, so an unchanged menu produces
// no D-Bus traffic at all.
void setLabel(GtkWidget* item, std::string& cache, const std::string& text) {
    if (cache == text) return;
    cache = text;
    gtk_menu_item_set_label(GTK_MENU_ITEM(item), text.c_str());
}

void setVisible(Row& row, bool visible) {
    if (row.visible == visible) return;
    row.visible = visible;
    gtk_widget_set_visible(row.item, visible);
}

void onMuteToggled(GtkCheckMenuItem* item, gpointer) {
    setMuted(gtk_check_menu_item_get_active(item));
}

void onTestBeep(GtkMenuItem*, gpointer) { playCue(Cue::Done); }

void onQuit(GtkMenuItem*, gpointer) { gtk_main_quit(); }

GtkWidget* appendLabel(GtkWidget* menu, const char* text, bool visible) {
    GtkWidget* item = gtk_menu_item_new_with_label(text);
    gtk_widget_set_sensitive(item, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_widget_set_visible(item, visible);
    return item;
}

GtkWidget* appendAction(GtkWidget* menu, const char* text, GCallback cb, gpointer data) {
    GtkWidget* item = gtk_menu_item_new_with_label(text);
    g_signal_connect(item, "activate", cb, data);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_widget_show(item);
    return item;
}

GtkWidget* appendSeparator(GtkWidget* menu, bool visible) {
    GtkWidget* item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_widget_set_visible(item, visible);
    return item;
}

// The full menu, laid out once with every row it will ever need.
void buildMenu(App* app) {
    app->menu = gtk_menu_new();

    app->headerLabel = summary(Status::Idle, 0);
    app->header = appendLabel(app->menu, app->headerLabel.c_str(), true);

    app->separator.item = appendSeparator(app->menu, false);

    app->rows.resize(kMaxRows);
    for (Row& row : app->rows) row.item = appendLabel(app->menu, "", false);

    appendSeparator(app->menu, true);

    app->mute = gtk_check_menu_item_new_with_label("Mute beeps");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(app->mute), muted());
    app->muteHandler = g_signal_connect(app->mute, "toggled", G_CALLBACK(onMuteToggled), nullptr);
    gtk_menu_shell_append(GTK_MENU_SHELL(app->menu), app->mute);
    gtk_widget_show(app->mute);

    appendAction(app->menu, "Test beep", G_CALLBACK(onTestBeep), nullptr);
    appendSeparator(app->menu, true);
    appendAction(app->menu, "Quit", G_CALLBACK(onQuit), app);

    gtk_widget_show(app->menu);
    app_indicator_set_menu(app->indicator, GTK_MENU(app->menu));
}

gboolean refresh(gpointer data) {
    App* app = static_cast<App*>(data);
    app->pending = 0;

    std::vector<Session> sessions = loadSessions(/*prune=*/true);
    // Stable ordering: sorting by timestamp made two equally busy sessions swap
    // places on every hook write, rewriting the menu for no visible reason.
    std::sort(sessions.begin(), sessions.end(), [](const Session& a, const Session& b) {
        if (a.status != b.status) return static_cast<int>(a.status) > static_cast<int>(b.status);
        if (a.cwd != b.cwd) return a.cwd < b.cwd;
        return a.id < b.id;
    });
    const Status overall = aggregate(sessions);

    if (!app->iconSet || overall != app->shown) {
        app_indicator_set_icon_full(app->indicator, iconName(overall), summary(overall, sessions.size()));
        app->shown = overall;
        app->iconSet = true;
        trace(app, std::string("icon -> ") + iconName(overall));
    }

    setLabel(app->header, app->headerLabel, summary(overall, sessions.size()));
    setVisible(app->separator, !sessions.empty());

    const size_t shownRows = std::min(sessions.size(), kMaxRows);
    for (size_t i = 0; i < kMaxRows; ++i) {
        Row& row = app->rows[i];
        const bool overflow = i + 1 == kMaxRows && sessions.size() > kMaxRows;
        if (i < shownRows) {
            setLabel(row.item, row.label,
                     overflow ? "… and " + std::to_string(sessions.size() - kMaxRows + 1) + " more"
                              : rowLabel(sessions[i]));
        }
        setVisible(row, i < shownRows);
    }

    // The tray toggle and `claude-beep mute` write the same flag, so mirror it
    // back without letting our own handler rewrite the file.
    const bool isMuted = muted();
    if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(app->mute)) != isMuted) {
        g_signal_handler_block(app->mute, app->muteHandler);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(app->mute), isMuted);
        g_signal_handler_unblock(app->mute, app->muteHandler);
    }

    return G_SOURCE_REMOVE;
}

void schedule(App* app) {
    if (app->pending) return;
    // Hooks write a temp file then rename; a short debounce collapses the burst.
    app->pending = g_timeout_add(120, refresh, app);
}

void onDirChanged(GFileMonitor*, GFile*, GFile*, GFileMonitorEvent, gpointer data) {
    schedule(static_cast<App*>(data));
}

gboolean onPoll(gpointer data) {
    // Catches sessions whose owning process died without a SessionEnd hook.
    schedule(static_cast<App*>(data));
    return G_SOURCE_CONTINUE;
}

} // namespace

int runIndicator() {
    gtk_init(nullptr, nullptr);

    App app;
    app.debug = getenv("CLAUDE_BEEP_DEBUG") != nullptr;
    const std::string icons = iconDir();

    app.indicator = app_indicator_new("claude-beep", iconName(Status::Idle),
                                      APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
    app_indicator_set_icon_theme_path(app.indicator, icons.c_str());
    app_indicator_set_title(app.indicator, "Claude");
    app_indicator_set_status(app.indicator, APP_INDICATOR_STATUS_ACTIVE);

    buildMenu(&app);

    GFile* dir = g_file_new_for_path(stateDir().c_str());
    GFileMonitor* monitor = g_file_monitor_directory(dir, G_FILE_MONITOR_NONE, nullptr, nullptr);
    if (monitor) g_signal_connect(monitor, "changed", G_CALLBACK(onDirChanged), &app);

    g_timeout_add_seconds(5, onPoll, &app);
    refresh(&app);

    gtk_main();

    if (monitor) g_object_unref(monitor);
    g_object_unref(dir);
    return 0;
}

} // namespace cb
