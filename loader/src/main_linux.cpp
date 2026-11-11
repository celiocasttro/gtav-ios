// gtav launcher (Linux) - first-run setup window (or terminal) and game launcher.
//
//   ./gtav_launcher                         installed: start the game; otherwise the setup window
//   ./gtav_launcher --setup                 the setup window again (installed or not)
//   ./gtav_launcher --setup DISC1 DISC2 TU  setup in the terminal with these paths
//   ./gtav_launcher --cli [--setup]         terminal setup even with a desktop (paths asked for)
//   ./gtav_launcher --unattended            setup from data/launcher.ini, no questions (testing)
//   ./gtav_launcher --iphone                after setup: iphone/GTAV.ipa for Sideloadly / AltStore
//   ./gtav_launcher -- ARGS...              pass ARGS to the game (for example --vulkan_device=1)
//
// Setup is the same as on Windows and macOS (install.cpp): the player's own Disc 1, Disc 2 and
// Title Update 27 are copied and checked, the game executable is recompiled on this machine into
// gtav_game.so, then the game starts. The window is GTK 3, loaded at run time (dlopen): the
// launcher needs only glibc, and without GTK or a display it falls back to the terminal.
#include <dlfcn.h>
#include <unistd.h>

#include <atomic>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef GTAV_GTK_UI
// Headers for the types only: every GTK/GLib function is called through dlsym pointers, and the
// cast macros must not call into GObject.
#define G_DISABLE_CAST_CHECKS
#include <gtk/gtk.h>
#endif

#include "install.h"
#include "sources.h"

namespace fs = std::filesystem;
using namespace loader;

namespace {

const char* kRowLabels[3] = {"Disc 1 (Install)", "Disc 2 (Play)", "Title Update 27"};
const char* kRowHints[3] = {
    "ISO image or extracted folder", "ISO image or extracted folder",
    "the TU file (e.g. tu00000009_00000000) or a folder with default.xexp + update.rpf"};

fs::path g_root;
volatile std::sig_atomic_t g_cancelled = 0;

fs::path SettingsPath() { return g_root / "data" / "launcher.ini"; }

std::vector<std::string> LoadInputs() {
  std::vector<std::string> v(3);
  std::ifstream f(SettingsPath());
  for (std::string& s : v) std::getline(f, s);
  return v;
}

void SaveInputs(const std::vector<std::string>& v) {
  std::error_code ec;
  fs::create_directories(g_root / "data", ec);
  std::ofstream f(SettingsPath());
  for (const std::string& s : v) f << s << "\n";
}

std::string Trim(std::string s) {
  while (!s.empty() && isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  size_t i = 0;
  while (i < s.size() && isspace(static_cast<unsigned char>(s[i]))) ++i;
  s.erase(0, i);
  // A path dropped into the terminal from a file manager arrives quoted.
  if (s.size() >= 2 && (s[0] == '\'' || s[0] == '"') && s.back() == s[0])
    s = s.substr(1, s.size() - 2);
  if (s.rfind("~/", 0) == 0) {
    if (const char* home = getenv("HOME")) s = std::string(home) + s.substr(1);
  }
  return s;
}

// Terminal UI for the install: stage lines, a progress line when stdout is a terminal, y/n
// questions on stdin, Ctrl+C cancels. Everything also goes to data/logs/install.log (install.cpp).
class TermUi : public InstallUi {
 public:
  TermUi() : tty_(isatty(STDOUT_FILENO)) {}
  void Stage(const std::wstring& t) override {
    std::string s = Narrow(t);
    if (s == last_stage_) return;
    last_stage_ = s;
    Line(s);
  }
  void Log(const std::wstring& t) override { Line("  " + Narrow(t)); }
  void Progress(double f) override {
    int v = int(f * 1000);
    if (v == last_ || !tty_) {
      if (!tty_ && v / 100 != last_ / 100) printf("progress: %d%%\n", v / 10);
      last_ = v;
      return;
    }
    last_ = v;
    int bar = v * 40 / 1000;
    printf("\r  [%.*s%*s] %5.1f%%", bar, "########################################", 40 - bar, "",
           v / 10.0);
    fflush(stdout);
    progress_shown_ = true;
  }
  bool Cancelled() override { return g_cancelled != 0; }
  bool Ask(const std::wstring& q) override {
    Line(Narrow(q));
    printf("[y/N] ");
    fflush(stdout);
    std::string answer;
    if (!std::getline(std::cin, answer)) return false;
    answer = Trim(answer);
    return answer == "y" || answer == "Y" || answer == "yes";
  }

 private:
  void Line(const std::string& s) {
    if (progress_shown_) {
      printf("\r%60s\r", "");
      progress_shown_ = false;
    }
    printf("%s\n", s.c_str());
    fflush(stdout);
  }
  bool tty_;
  bool progress_shown_ = false;
  int last_ = -1;
  std::string last_stage_;
};

// "--unattended": installs from the three paths in data/launcher.ini (one per line), writes the
// stage and progress lines to data/logs/unattended.txt, says no to every question and returns 0 on
// success. For testing a release and for scripted installs. Same as the Windows launcher.
class FileUi : public InstallUi {
 public:
  explicit FileUi(const fs::path& path) : f_(path, std::ios::app) {}
  void Stage(const std::wstring& t) override { Write(L"stage: " + t); }
  void Log(const std::wstring& t) override { Write(t); }
  void Progress(double f) override {
    int v = int(f * 100);
    if (v != last_) Write(L"progress: " + std::to_wstring(last_ = v) + L"%");
  }
  bool Cancelled() override { return g_cancelled != 0; }
  bool Ask(const std::wstring& q) override {
    Write(L"question (answered no): " + q);
    return false;
  }

 private:
  void Write(const std::wstring& s) {
    std::lock_guard lock(m_);
    f_ << Narrow(s) << "\n";
    f_.flush();
  }
  std::mutex m_;
  std::ofstream f_;
  int last_ = -1;
};

int Unattended() {
  std::error_code ec;
  fs::create_directories(g_root / "data" / "logs", ec);
  FileUi ui(g_root / "data" / "logs" / "unattended.txt");
  std::wstring error;
  if (!Preflight(g_root, &error)) {
    ui.Log(L"error: " + error);
    return 1;
  }
  std::vector<std::string> v = LoadInputs();
  if (v[0].empty() || v[1].empty() || v[2].empty()) {
    ui.Log(L"error: data/launcher.ini needs three lines: Disc 1, Disc 2, Title Update");
    return 1;
  }
  InstallInputs in{Widen(v[0]), Widen(v[1]), Widen(v[2])};
  bool ok = RunInstall(g_root, in, ui, &error);
  ui.Log(ok ? L"done" : L"error: " + error);
  return ok ? 0 : 1;
}

void AddDesktopEntry(InstallUi* ui);

int Setup(std::vector<std::string> given, const std::vector<std::wstring>& game_args) {
  printf("GTA V recomp setup\n\n"
         "This builds the game from your own Xbox 360 copy of GTA V: Disc 1 (Install), Disc 2\n"
         "(Play) and Title Update 27. Nothing is downloaded. Press Ctrl+C to stop; running setup\n"
         "again continues where it stopped.\n\n");
  std::vector<std::string> saved = LoadInputs();
  std::vector<std::string> paths(3);
  for (int i = 0; i < 3; ++i) {
    if (i < int(given.size())) {
      paths[i] = Trim(given[i]);
      continue;
    }
    printf("%s - %s\n", kRowLabels[i], kRowHints[i]);
    if (!saved[i].empty()) printf("  [Enter: %s]\n", saved[i].c_str());
    printf("> ");
    fflush(stdout);
    std::string line;
    if (!std::getline(std::cin, line)) return 1;
    paths[i] = Trim(line).empty() ? saved[i] : Trim(line);
    if (paths[i].empty()) {
      fprintf(stderr, "No path given for %s.\n", kRowLabels[i]);
      return 1;
    }
  }
  for (int i = 0; i < 3; ++i) {
    std::error_code ec;
    if (!fs::exists(paths[i], ec)) {
      fprintf(stderr, "%s: %s does not exist.\n", kRowLabels[i], paths[i].c_str());
      return 1;
    }
    paths[i] = fs::absolute(paths[i], ec).string();
  }
  SaveInputs(paths);

  printf("\n");
  TermUi ui;
  std::wstring error;
  InstallInputs in{Widen(paths[0]), Widen(paths[1]), Widen(paths[2])};
  if (!RunInstall(g_root, in, ui, &error)) {
    fprintf(stderr, "\nSetup stopped: %s\n(log: %s)\n", Narrow(error).c_str(),
            (g_root / "data" / "logs" / "install.log").c_str());
    return 1;
  }
  AddDesktopEntry(&ui);
  printf("\nSetup finished. Start the game now? [Y/n] ");
  fflush(stdout);
  std::string answer;
  if (std::getline(std::cin, answer) && (Trim(answer) == "n" || Trim(answer) == "N")) return 0;
  if (!LaunchGame(g_root, &error, game_args)) {
    fprintf(stderr, "%s\n", Narrow(error).c_str());
    return 1;
  }
  return 0;
}

// "GTA V recomp" in the desktop's applications menu (~/.local/share/applications), pointing at
// this launcher, with a "Run setup again" action. Rewritten after every setup (the folder may move).
void AddDesktopEntry(InstallUi* ui) {
  const char* home = getenv("HOME");
  if (!home || !*home) return;
  const char* data = getenv("XDG_DATA_HOME");
  fs::path dir = data && *data ? fs::path(data) / "applications" : fs::path(home) / ".local/share/applications";
  std::error_code ec;
  fs::create_directories(dir, ec);
  // Exec quoting (Desktop Entry spec): the path in double quotes, \ " ` $ escaped.
  std::string exe;
  for (char c : (g_root / "gtav_launcher").string()) {
    if (c == '"' || c == '`' || c == '$' || c == '\\') exe += '\\';
    exe += c;
  }
  std::ofstream f(dir / "gtav-recomp.desktop");
  f << "[Desktop Entry]\nType=Application\nName=GTA V recomp\n"
       "Comment=GTA V for Xbox 360, recompiled from your own copy\n"
    << "Exec=\"" << exe << "\"\nPath=" << g_root.string() << "\n"
    << "Icon=applications-games\nTerminal=false\nCategories=Game;\nActions=Setup;\n\n"
       "[Desktop Action Setup]\nName=Run setup again\n"
    << "Exec=\"" << exe << "\" --setup\n";
  f.close();
  if (f && ui) ui->Log(L"Added \"GTA V recomp\" to the applications menu.");
}

bool HaveDisplay() {
  const char* x = getenv("DISPLAY");
  const char* w = getenv("WAYLAND_DISPLAY");
  return (x && *x) || (w && *w);
}

#ifdef GTAV_GTK_UI
//-----------------------------------------------------------------------------
// Setup window (GTK 3 through dlopen)
//-----------------------------------------------------------------------------

struct Gtk {
#define GTAV_GTK_FUNCTIONS(X)                                                                        \
  X(gtk_init_check) X(gtk_main) X(gtk_main_quit) X(gtk_events_pending) X(gtk_main_iteration)        \
  X(gtk_window_new) X(gtk_window_set_title) X(gtk_window_set_default_size) X(gtk_window_set_position) \
  X(gtk_container_add) X(gtk_container_set_border_width) X(gtk_box_new) X(gtk_box_pack_start)       \
  X(gtk_box_set_spacing) X(gtk_label_new) X(gtk_label_set_line_wrap) X(gtk_label_set_xalign)        \
  X(gtk_label_set_text) X(gtk_label_set_markup) X(gtk_entry_new) X(gtk_entry_set_text)              \
  X(gtk_entry_get_text) X(gtk_editable_set_editable) X(gtk_button_new_with_label)                   \
  X(gtk_button_set_label) X(gtk_widget_set_sensitive) X(gtk_widget_show_all) X(gtk_widget_hide)     \
  X(gtk_widget_destroy) X(gtk_widget_set_hexpand) X(gtk_widget_set_vexpand)                          \
  X(gtk_progress_bar_new) X(gtk_progress_bar_set_fraction) X(gtk_scrolled_window_new)               \
  X(gtk_scrolled_window_set_policy) X(gtk_text_view_new) X(gtk_text_view_set_editable)              \
  X(gtk_text_view_set_monospace) X(gtk_text_view_set_cursor_visible) X(gtk_text_view_get_buffer)    \
  X(gtk_text_view_scroll_mark_onscreen) X(gtk_text_buffer_get_end_iter) X(gtk_text_buffer_insert)   \
  X(gtk_text_buffer_place_cursor) X(gtk_text_buffer_get_insert) X(gtk_file_chooser_dialog_new)      \
  X(gtk_file_chooser_get_filename) X(gtk_file_chooser_set_filename) X(gtk_dialog_run)               \
  X(gtk_message_dialog_new) X(gtk_button_box_new) X(gtk_button_box_set_layout) X(g_signal_connect_data) \
  X(g_idle_add) X(g_free)
#define GTAV_GTK_MEMBER(name) decltype(&::name) name = nullptr;
  GTAV_GTK_FUNCTIONS(GTAV_GTK_MEMBER)
#undef GTAV_GTK_MEMBER

  bool Load() {
    void* h = dlopen("libgtk-3.so.0", RTLD_NOW | RTLD_GLOBAL);
    if (!h) return false;
    bool ok = true;
#define GTAV_GTK_LOAD(name) ok = ok && (name = reinterpret_cast<decltype(name)>(dlsym(h, #name)));
    GTAV_GTK_FUNCTIONS(GTAV_GTK_LOAD)
#undef GTAV_GTK_LOAD
    return ok;
  }
};
Gtk g;

// Runs f on the GTK main loop (from the install thread).
void OnMain(std::function<void()> f) {
  g.g_idle_add(
      [](gpointer p) -> gboolean {
        auto* fn = static_cast<std::function<void()>*>(p);
        (*fn)();
        delete fn;
        return FALSE;
      },
      new std::function<void()>(std::move(f)));
}

GtkWidget* g_window = nullptr;

bool Question(const std::string& text) {
  GtkWidget* d = g.gtk_message_dialog_new(GTK_WINDOW(g_window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                                          GTK_BUTTONS_YES_NO, "%s", text.c_str());
  g.gtk_window_set_title(GTK_WINDOW(d), "GTA V recomp setup");
  bool yes = g.gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_YES;
  g.gtk_widget_destroy(d);
  return yes;
}

void ErrorBox(const std::string& text) {
  GtkWidget* d = g.gtk_message_dialog_new(g_window ? GTK_WINDOW(g_window) : nullptr, GTK_DIALOG_MODAL,
                                          GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", text.c_str());
  g.gtk_window_set_title(GTK_WINDOW(d), "GTA V recomp");
  g.gtk_dialog_run(GTK_DIALOG(d));
  g.gtk_widget_destroy(d);
}

template <class F>
void Connect(gpointer widget, const char* signal, F handler, gpointer data = nullptr) {
  g.g_signal_connect_data(widget, signal, G_CALLBACK(handler), data, nullptr, GConnectFlags(0));
}

class SetupWindow : public InstallUi {
 public:
  explicit SetupWindow(std::vector<std::wstring> game_args) : game_args_(std::move(game_args)) {}

  void Build() {
    g_window = window_ = g.gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g.gtk_window_set_title(GTK_WINDOW(window_), "GTA V recomp setup");
    g.gtk_window_set_default_size(GTK_WINDOW(window_), 760, 620);
    g.gtk_window_set_position(GTK_WINDOW(window_), GTK_WIN_POS_CENTER);
    g.gtk_container_set_border_width(GTK_CONTAINER(window_), 16);
    Connect(window_, "delete-event", +[](GtkWidget*, GdkEvent*, gpointer self) -> gboolean {
      return static_cast<SetupWindow*>(self)->CloseRequested() ? FALSE : TRUE;
    }, this);
    Connect(window_, "destroy", +[](GtkWidget*, gpointer) { g.gtk_main_quit(); });

    GtkWidget* box = g.gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    g.gtk_container_add(GTK_CONTAINER(window_), box);
    GtkWidget* intro = g.gtk_label_new(
        "Point setup at your own copy of the game: both Xbox 360 discs and Title Update 27. Setup copies "
        "the game data into this folder, recompiles the game executable on this computer and checks "
        "every file. Nothing is downloaded and nothing is sent anywhere. It needs about 25 GB free "
        "during setup (17 GB after) and 15-40 minutes.");
    g.gtk_label_set_line_wrap(GTK_LABEL(intro), TRUE);
    g.gtk_label_set_xalign(GTK_LABEL(intro), 0);
    g.gtk_box_pack_start(GTK_BOX(box), intro, FALSE, FALSE, 4);

    std::vector<std::string> saved = LoadInputs();
    for (int i = 0; i < 3; ++i) {
      GtkWidget* label = g.gtk_label_new(nullptr);
      std::string markup = std::string("<b>") + kRowLabels[i] + "</b>  <span alpha=\"60%\">" + kRowHints[i] + "</span>";
      g.gtk_label_set_markup(GTK_LABEL(label), Escape(markup).c_str());
      g.gtk_label_set_xalign(GTK_LABEL(label), 0);
      g.gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, i ? 6 : 10);
      GtkWidget* row = g.gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
      paths_[i] = g.gtk_entry_new();
      g.gtk_entry_set_text(GTK_ENTRY(paths_[i]), saved[i].c_str());
      g.gtk_widget_set_hexpand(paths_[i], TRUE);
      g.gtk_box_pack_start(GTK_BOX(row), paths_[i], TRUE, TRUE, 0);
      file_[i] = g.gtk_button_new_with_label("Choose file...");
      folder_[i] = g.gtk_button_new_with_label("Folder...");
      Connect(file_[i], "clicked", +[](GtkButton* b, gpointer self) { static_cast<SetupWindow*>(self)->Choose(b, false); }, this);
      Connect(folder_[i], "clicked", +[](GtkButton* b, gpointer self) { static_cast<SetupWindow*>(self)->Choose(b, true); }, this);
      g.gtk_box_pack_start(GTK_BOX(row), file_[i], FALSE, FALSE, 0);
      g.gtk_box_pack_start(GTK_BOX(row), folder_[i], FALSE, FALSE, 0);
      g.gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
    }

    stage_ = g.gtk_label_new("");
    g.gtk_label_set_xalign(GTK_LABEL(stage_), 0);
    g.gtk_box_pack_start(GTK_BOX(box), stage_, FALSE, FALSE, 10);
    progress_ = g.gtk_progress_bar_new();
    g.gtk_box_pack_start(GTK_BOX(box), progress_, FALSE, FALSE, 0);

    GtkWidget* scroll = g.gtk_scrolled_window_new(nullptr, nullptr);
    g.gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    g.gtk_widget_set_vexpand(scroll, TRUE);
    log_ = g.gtk_text_view_new();
    g.gtk_text_view_set_editable(GTK_TEXT_VIEW(log_), FALSE);
    g.gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(log_), FALSE);
    g.gtk_text_view_set_monospace(GTK_TEXT_VIEW(log_), TRUE);
    g.gtk_container_add(GTK_CONTAINER(scroll), log_);
    g.gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 6);

    GtkWidget* buttons = g.gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    g.gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    g.gtk_box_set_spacing(GTK_BOX(buttons), 8);
    install_ = g.gtk_button_new_with_label("Install");
    play_ = g.gtk_button_new_with_label("Play");
    // Phone builds from the installed game (install.h RunIphoneBuild / RunAndroidBuild).
    iphone_ = g.gtk_button_new_with_label("Build for iPhone");
    android_ = g.gtk_button_new_with_label("Build for Android");
    Connect(install_, "clicked", +[](GtkButton*, gpointer self) { static_cast<SetupWindow*>(self)->InstallClicked(); }, this);
    Connect(play_, "clicked", +[](GtkButton*, gpointer self) { static_cast<SetupWindow*>(self)->Play(); }, this);
    Connect(iphone_, "clicked", +[](GtkButton*, gpointer self) { static_cast<SetupWindow*>(self)->PhoneClicked(false); }, this);
    Connect(android_, "clicked", +[](GtkButton*, gpointer self) { static_cast<SetupWindow*>(self)->PhoneClicked(true); }, this);
    g.gtk_container_add(GTK_CONTAINER(buttons), iphone_);
    g.gtk_container_add(GTK_CONTAINER(buttons), android_);
    g.gtk_container_add(GTK_CONTAINER(buttons), install_);
    g.gtk_container_add(GTK_CONTAINER(buttons), play_);
    g.gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);

    SetBusy(false);
    g.gtk_label_set_text(GTK_LABEL(stage_), IsInstalled(g_root) ? "Installed. Press Play, or Install to redo setup."
                                                                : "Pick the three inputs, then press Install.");
    g.gtk_widget_show_all(window_);
  }

  // InstallUi: called on the install thread.
  void Stage(const std::wstring& t) override {
    std::string s = Narrow(t);
    OnMain([this, s] { g.gtk_label_set_text(GTK_LABEL(stage_), s.c_str()); });
  }
  void Log(const std::wstring& t) override {
    std::string s = Narrow(t);
    OnMain([this, s] { Append(s); });
  }
  void Progress(double f) override {
    int v = int(f * 1000);
    if (v == last_) return;
    last_ = v;
    OnMain([this, f] { g.gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_), f); });
  }
  bool Cancelled() override { return cancelled_ || g_cancelled; }
  bool Ask(const std::wstring& q) override {
    std::string s = Narrow(q);
    std::mutex m;
    std::condition_variable cv;
    int answer = -1;
    OnMain([&] {
      bool yes = Question(s);
      std::lock_guard lock(m);
      answer = yes;
      cv.notify_one();
    });
    std::unique_lock lock(m);
    cv.wait(lock, [&] { return answer >= 0; });
    return answer == 1;
  }

 private:
  // Markup: the labels and hints are ours except for '&', which must be an entity.
  static std::string Escape(const std::string& s) {
    std::string out;
    for (char c : s) out += c == '&' ? std::string("&amp;") : std::string(1, c);
    return out;
  }

  void Append(const std::string& s) {
    GtkTextBuffer* buf = g.gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_));
    GtkTextIter end;
    g.gtk_text_buffer_get_end_iter(buf, &end);
    std::string line = s + "\n";
    g.gtk_text_buffer_insert(buf, &end, line.c_str(), -1);
    g.gtk_text_buffer_get_end_iter(buf, &end);
    g.gtk_text_buffer_place_cursor(buf, &end);
    g.gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(log_), g.gtk_text_buffer_get_insert(buf));
  }

  void SetBusy(bool busy) {
    busy_ = busy;
    for (int i = 0; i < 3; ++i) {
      g.gtk_widget_set_sensitive(file_[i], !busy);
      g.gtk_widget_set_sensitive(folder_[i], !busy);
      g.gtk_editable_set_editable(GTK_EDITABLE(paths_[i]), !busy);
    }
    const bool installed = IsInstalled(g_root);
    g.gtk_button_set_label(GTK_BUTTON(install_), busy ? "Cancel" : "Install");
    g.gtk_widget_set_sensitive(install_, !busy || phone_ < 0);
    g.gtk_widget_set_sensitive(play_, !busy && installed);
    g.gtk_widget_set_sensitive(iphone_, (!busy && installed) || (busy && phone_ == 0));
    g.gtk_widget_set_sensitive(android_, (!busy && installed) || (busy && phone_ == 1));
    g.gtk_button_set_label(GTK_BUTTON(iphone_), busy && phone_ == 0 ? "Cancel" : "Build for iPhone");
    g.gtk_button_set_label(GTK_BUTTON(android_), busy && phone_ == 1 ? "Cancel" : "Build for Android");
  }

  // Build for iPhone / Android: the game's own install must be done first (the button is greyed
  // out until then). Runs on a thread like the install; the same button cancels it.
  void PhoneClicked(bool android) {
    if (busy_) {
      cancelled_ = true;
      g.gtk_label_set_text(GTK_LABEL(stage_), "Cancelling...");
      return;
    }
    cancelled_ = false;
    last_ = -1;
    phone_ = android ? 1 : 0;
    SetBusy(true);
    std::thread([this, android] {
      std::wstring err;
      bool ok = android ? RunAndroidBuild(g_root, g_root, *this, &err) : RunIphoneBuild(g_root, g_root, *this, &err);
      std::string e = Narrow(err);
      OnMain([this, ok, e, android] { PhoneFinished(ok, e, android); });
    }).detach();
  }

  void PhoneFinished(bool ok, const std::string& error, bool android) {
    phone_ = -1;
    SetBusy(false);
    if (ok) {
      g.gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_), 1.0);
      return;  // the build's last Stage() says where the file is
    }
    if (cancelled_) {
      g.gtk_label_set_text(GTK_LABEL(stage_), "Cancelled.");
      if (quit_when_stopped_) g.gtk_widget_destroy(window_);
      return;
    }
    g.gtk_label_set_text(GTK_LABEL(stage_), android ? "Android build stopped." : "iPhone build stopped.");
    ErrorBox(error + "\n\n(log: " + (g_root / "data" / "logs" / (android ? "android.log" : "iphone.log")).string() + ")");
  }

  void Choose(GtkButton* button, bool folder) {
    int i = 0;
    while (i < 3 && GTK_WIDGET(button) != (folder ? folder_[i] : file_[i])) ++i;
    if (i == 3) return;
    std::string title = std::string(kRowLabels[i]) + ": " + kRowHints[i];
    GtkWidget* d = g.gtk_file_chooser_dialog_new(
        title.c_str(), GTK_WINDOW(window_),
        folder ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER : GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel",
        GTK_RESPONSE_CANCEL, folder ? "_Select" : "_Open", GTK_RESPONSE_ACCEPT, nullptr);
    std::string current = g.gtk_entry_get_text(GTK_ENTRY(paths_[i]));
    std::error_code ec;
    if (!current.empty() && fs::exists(current, ec)) g.gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(d), current.c_str());
    if (g.gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT) {
      if (gchar* name = g.gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d))) {
        g.gtk_entry_set_text(GTK_ENTRY(paths_[i]), name);
        (g.g_free)(name);  // parenthesised: g_free is also a function-like macro in newer GLib
      }
    }
    g.gtk_widget_destroy(d);
  }

  void InstallClicked() {
    if (busy_) {
      cancelled_ = true;
      g.gtk_label_set_text(GTK_LABEL(stage_), "Cancelling...");
      return;
    }
    std::vector<std::string> paths(3);
    for (int i = 0; i < 3; ++i) {
      paths[i] = Trim(g.gtk_entry_get_text(GTK_ENTRY(paths_[i])));
      std::error_code ec;
      if (paths[i].empty() || !fs::exists(paths[i], ec)) {
        ErrorBox(std::string(kRowLabels[i]) + ": pick the image, file or folder.");
        return;
      }
      paths[i] = fs::absolute(paths[i], ec).string();
    }
    SaveInputs(paths);
    cancelled_ = false;
    last_ = -1;
    SetBusy(true);
    InstallInputs in{Widen(paths[0]), Widen(paths[1]), Widen(paths[2])};
    std::thread([this, in] {
      std::wstring err;
      bool ok = RunInstall(g_root, in, *this, &err);
      if (ok) AddDesktopEntry(this);
      std::string e = Narrow(err);
      OnMain([this, ok, e] { Finished(ok, e); });
    }).detach();
  }

  void Finished(bool ok, const std::string& error) {
    SetBusy(false);
    if (ok) {
      g.gtk_label_set_text(GTK_LABEL(stage_), "Setup finished. Press Play.");
      g.gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_), 1.0);
      return;
    }
    if (cancelled_) {
      g.gtk_label_set_text(GTK_LABEL(stage_), "Cancelled. Install again to continue where it stopped.");
      if (quit_when_stopped_) g.gtk_widget_destroy(window_);
      return;
    }
    g.gtk_label_set_text(GTK_LABEL(stage_), "Setup stopped.");
    ErrorBox(error + "\n\n(log: " + (g_root / "data" / "logs" / "install.log").string() + ")");
  }

  bool CloseRequested() {
    if (!busy_) return true;
    if (Question("Setup is running. Stop it and close? Running setup again continues where it stopped.")) {
      cancelled_ = true;
      quit_when_stopped_ = true;  // the window closes when the install thread has stopped
    }
    return false;
  }

  void Play() {
    // The launcher process becomes the game (exec): take the window down first.
    g.gtk_widget_hide(window_);
    while (g.gtk_events_pending()) g.gtk_main_iteration();
    std::wstring error;
    if (LaunchGame(g_root, &error, game_args_)) return;
    g.gtk_widget_show_all(window_);
    ErrorBox(Narrow(error));
  }

  std::vector<std::wstring> game_args_;
  GtkWidget* window_ = nullptr;
  GtkWidget* paths_[3] = {};
  GtkWidget* file_[3] = {};
  GtkWidget* folder_[3] = {};
  GtkWidget* stage_ = nullptr;
  GtkWidget* progress_ = nullptr;
  GtkWidget* log_ = nullptr;
  GtkWidget* install_ = nullptr;
  GtkWidget* play_ = nullptr;
  GtkWidget* iphone_ = nullptr;
  GtkWidget* android_ = nullptr;
  int phone_ = -1;  // the phone build running: 0 iPhone, 1 Android (-1: none, or the install)
  bool busy_ = false;
  bool quit_when_stopped_ = false;
  std::atomic<bool> cancelled_{false};
  int last_ = -1;
};

bool LoadGui() {
  static int loaded = -1;
  if (loaded < 0) loaded = HaveDisplay() && g.Load() && g.gtk_init_check(nullptr, nullptr);
  return loaded == 1;
}

// The setup window until it is closed (or the game starts). false: no GTK / display.
bool RunSetupWindow(const std::vector<std::wstring>& game_args, int* code) {
  if (!LoadGui()) return false;
  std::wstring error;
  if (!Preflight(g_root, &error)) {
    ErrorBox(Narrow(error));
    *code = 1;
    return true;
  }
  SetupWindow w(game_args);
  w.Build();
  g.gtk_main();
  *code = 0;
  return true;
}

// Errors when there may be no terminal to see them (started from the desktop).
void ShowError(const std::string& text) {
  fprintf(stderr, "%s\n", text.c_str());
  if (LoadGui()) ErrorBox(text);
}
#else
bool RunSetupWindow(const std::vector<std::wstring>&, int*) { return false; }
void ShowError(const std::string& text) { fprintf(stderr, "%s\n", text.c_str()); }
#endif

}  // namespace

int main(int argc, char** argv) {
  std::error_code ec;
  g_root = fs::read_symlink("/proc/self/exe", ec).parent_path();
  if (ec) g_root = fs::absolute(argv[0]).parent_path();
  std::signal(SIGINT, [](int) { g_cancelled = 1; });

  bool setup = false, unattended = false, cli = false, iphone = false, android = false;
  std::vector<std::string> paths;
  std::vector<std::wstring> game_args;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--") {
      for (++i; i < argc; ++i) game_args.push_back(Widen(argv[i]));
      break;
    }
    if (a == "--setup") {
      setup = true;
    } else if (a == "--unattended") {
      unattended = true;
    } else if (a == "--cli") {
      cli = true;
    } else if (a == "--iphone") {
      iphone = true;
    } else if (a == "--android") {
      android = true;
    } else if (a == "--help" || a == "-h") {
      printf("usage: %s [--setup [DISC1 DISC2 TU]] [--cli] [--unattended] [--iphone] [--android] [-- GAME_ARGS...]\n", argv[0]);
      return 0;
    } else if (a.rfind("--", 0) == 0) {
      fprintf(stderr, "unknown option %s (game options go after --)\n", a.c_str());
      return 2;
    } else {
      paths.push_back(a);
    }
  }
  if (unattended) return Unattended();
  if (iphone) {
    // The iPhone build (install.h RunIphoneBuild) from the installed game.
    std::wstring error;
    TermUi ui;
    if (!Preflight(g_root, &error) || !RunIphoneBuild(g_root, g_root, ui, &error)) {
      fprintf(stderr, "\niPhone build stopped: %s\n", Narrow(error).c_str());
      return 1;
    }
    printf("\nDone: %s\nInstall it on the phone with Sideloadly or AltStore, then open GTA V there: it\n"
           "installs the game files from your discs itself.\n",
           (g_root / "iphone" / "GTAV.ipa").c_str());
    return 0;
  }
  if (android) {
    // The Android build (install.h RunAndroidBuild) from the installed game.
    std::wstring error;
    TermUi ui;
    if (!Preflight(g_root, &error) || !RunAndroidBuild(g_root, g_root, ui, &error)) {
      fprintf(stderr, "\nAndroid build stopped: %s\n", Narrow(error).c_str());
      return 1;
    }
    printf("\nDone: %s\nCopy it and your disc files to the phone (Download folder), install the GTA V\n"
           "APK, open it and press Install: it installs the game files from your discs itself.\n",
           (g_root / "android" / "gtav-android-pack.zip").c_str());
    return 0;
  }
  if (!paths.empty()) setup = true;
  const bool window = !cli && paths.empty();

  std::wstring error;
  if (!setup && IsInstalled(g_root)) {
    if (LaunchGame(g_root, &error, game_args)) return 0;
    if (window) {
      ShowError(Narrow(error));
    } else {
      fprintf(stderr, "%s\n", Narrow(error).c_str());
    }
    return 1;
  }
  int code = 0;
  if (window && RunSetupWindow(game_args, &code)) return code;
  if (!Preflight(g_root, &error)) {
    fprintf(stderr, "%s\n", Narrow(error).c_str());
    return 1;
  }
  return Setup(paths, game_args);
}
