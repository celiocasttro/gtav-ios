// gtav - iPhone setup screen (see ios_setup.h).
//
// The player's discs and title update are read where they are: in the app's Documents (copied in
// with Finder file sharing or the Files app), or anywhere the Files picker reaches (iCloud Drive, a
// USB drive plugged into the phone) through a security-scoped URL held for the life of the process.
// The copy itself is the Mac/PC installer's (loader/src/install.cpp RunDataInstall): every file
// checked against the known-good hashes in the app's expected.txt, an interrupted install continues
// where it stopped. Shown through ReXApp::OnFinalizePaths, which holds the game's start until the
// data is in place.

#include "ios_setup.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../../loader/src/install.h"
#include "../../loader/src/sources.h"

#import <SDL3/SDL.h>
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

namespace fs = std::filesystem;

namespace gtav::ios_setup {
namespace {

fs::path Documents() {
  const char* home = std::getenv("HOME");
  return home ? fs::path(home) / "Documents" : fs::current_path();
}

NSString* NS(const std::wstring& s) { return [NSString stringWithUTF8String:loader::Narrow(s).c_str()]; }
NSString* NS(const fs::path& p) { return [NSString stringWithUTF8String:p.c_str()]; }

enum Slot { kDisc1, kDisc2, kTu, kSlots };
const char* kSlotNames[kSlots] = {"Disc 1 (Install)", "Disc 2 (Play)", "Title Update 27"};

// What a file or folder is: a disc (and which), a title update, or nothing we know.
int Classify(const fs::path& p) {
  std::wstring err;
  std::error_code ec;
  const bool dir = fs::is_directory(p, ec);
  if (dir || p.extension() == ".iso" || p.extension() == ".ISO") {
    if (auto disc = loader::OpenDisc(p.wstring(), &err)) {
      // As install.cpp OpenInputs: Disc 1 carries the install packages (and a default.xex and
      // xbox360b.rpf of its own, so it is tested first); Disc 2 has the play data.
      if (disc->Find("content/0000000000000000/545408A7/00000002/545408A700000000")) return kDisc1;
      if (disc->Find("default.xex") && disc->Find("xbox360b.rpf") && disc->Find("audio_rel.rpf")) return kDisc2;
      if (disc->Find("disc002/default.xexp") || disc->Find("default.xexp")) return kTu;
    }
    return -1;
  }
  if (fs::file_size(p, ec) > (512ull << 20)) return -1;
  auto blob = loader::OpenFileBlob(p.wstring(), &err);
  if (!blob || !loader::LooksLikeStfs(*blob)) return -1;
  auto stfs = loader::OpenStfs(std::move(blob), &err);
  if (stfs && (stfs->Find("disc002/default.xexp") || stfs->Find("default.xexp"))) return kTu;
  return -1;
}

// Inputs the player already put in Documents (or Documents/Inbox, where "Open in" lands).
void FindPlaced(fs::path found[kSlots]) {
  for (const fs::path& dir : {Documents(), Documents() / "Inbox"}) {
    std::error_code ec;
    for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
      const std::string name = it->path().filename().string();
      if (name == "data" || name == "logs" || name == "shader_cache" || name == "spirv" || name == "Inbox" ||
          name[0] == '.')
        continue;
      int slot = Classify(it->path());
      if (slot >= 0 && found[slot].empty()) found[slot] = it->path();
    }
  }
}

UIViewController* TopController() {
  UIWindow* window = nil;
  if (SDL_Window* w = SDL_GetKeyboardFocus())
    window = (__bridge UIWindow*)SDL_GetPointerProperty(SDL_GetWindowProperties(w),
                                                         SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
  if (!window) {
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes)
      if ([scene isKindOfClass:UIWindowScene.class])
        for (UIWindow* w in ((UIWindowScene*)scene).windows)
          if (w.isKeyWindow) window = w;
  }
  UIViewController* c = window.rootViewController;
  while (c.presentedViewController) c = c.presentedViewController;
  return c;
}

}  // namespace
}  // namespace gtav::ios_setup

using namespace gtav::ios_setup;

@interface GtavSetupController : UIViewController <UIDocumentPickerDelegate>
@property(nonatomic, copy) void (^done)(void);
- (void)stage:(NSString*)s;
- (void)appendLog:(NSString*)s;
- (void)progress:(double)f;
- (BOOL)askBlocking:(NSString*)q;
@end

namespace {
// InstallUi for the screen: called on the install thread, marshalled to the main thread.
class ScreenUi : public loader::InstallUi {
 public:
  explicit ScreenUi(GtavSetupController* c) : c_(c) {}
  void Stage(const std::wstring& t) override {
    NSString* s = NS(t);
    dispatch_async(dispatch_get_main_queue(), ^{ [c_ stage:s]; });
  }
  void Log(const std::wstring& t) override {
    NSString* s = NS(t);
    dispatch_async(dispatch_get_main_queue(), ^{ [c_ appendLog:s]; });
  }
  void Progress(double f) override {
    int v = int(f * 1000);
    if (v == last_) return;
    last_ = v;
    dispatch_async(dispatch_get_main_queue(), ^{ [c_ progress:f]; });
  }
  bool Cancelled() override { return cancelled; }
  bool Ask(const std::wstring& q) override { return [c_ askBlocking:NS(q)]; }
  std::atomic<bool> cancelled{false};

 private:
  __weak GtavSetupController* c_;
  int last_ = -1;
};

NSMutableArray<NSURL*>* g_scoped;  // security-scoped picks, held for the process's life
}  // namespace

@implementation GtavSetupController {
  fs::path _inputs[kSlots];
  UILabel* _paths[kSlots];
  UIButton* _choose[kSlots];
  UIButton* _install;
  UILabel* _stage;
  UIProgressView* _progress;
  UITextView* _log;
  int _picking;
  bool _busy;
  std::unique_ptr<ScreenUi> _ui;
  UIBackgroundTaskIdentifier _bg;
}

- (UILabel*)label:(NSString*)text size:(CGFloat)size bold:(BOOL)bold {
  UILabel* l = [[UILabel alloc] init];
  l.text = text;
  l.numberOfLines = 0;
  l.textColor = UIColor.labelColor;
  l.font = bold ? [UIFont boldSystemFontOfSize:size] : [UIFont systemFontOfSize:size];
  return l;
}

- (UIButton*)button:(NSString*)title action:(SEL)sel filled:(BOOL)filled {
  UIButtonConfiguration* cfg = filled ? UIButtonConfiguration.filledButtonConfiguration
                                      : UIButtonConfiguration.tintedButtonConfiguration;
  cfg.title = title;
  UIButton* b = [UIButton buttonWithConfiguration:cfg primaryAction:nil];
  [b addTarget:self action:sel forControlEvents:UIControlEventTouchUpInside];
  return b;
}

- (void)viewDidLoad {
  [super viewDidLoad];
  self.view.backgroundColor = UIColor.systemBackgroundColor;
  self.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;

  UIStackView* left = [[UIStackView alloc] init];
  left.axis = UILayoutConstraintAxisVertical;
  left.spacing = 10;
  [left addArrangedSubview:[self label:@"GTA V - game data" size:22 bold:YES]];
  [left addArrangedSubview:[self label:@"This app holds the game code your Mac built, but not the game's files. "
                                       @"Install them from your own Disc 1, Disc 2 and Title Update 27: put the two "
                                       @"ISO images and the TU file (tu00000009_00000000) in GTA V's folder "
                                       @"(Finder > your iPhone > Files, or the Files app), or pick them anywhere "
                                       @"with Choose (a USB drive works). About 16 GB is copied and checked."
                                  size:13
                                  bold:NO]];
  for (int i = 0; i < kSlots; i++) {
    UIStackView* row = [[UIStackView alloc] init];
    row.axis = UILayoutConstraintAxisHorizontal;
    row.spacing = 10;
    row.alignment = UIStackViewAlignmentCenter;
    UILabel* name = [self label:[NSString stringWithUTF8String:kSlotNames[i]] size:15 bold:YES];
    [name.widthAnchor constraintEqualToConstant:140].active = YES;
    _paths[i] = [self label:@"not found" size:13 bold:NO];
    _paths[i].textColor = UIColor.secondaryLabelColor;
    _paths[i].lineBreakMode = NSLineBreakByTruncatingMiddle;
    _paths[i].numberOfLines = 1;
    _choose[i] = [self button:@"Choose" action:@selector(choose:) filled:NO];
    _choose[i].tag = i;
    [_paths[i] setContentCompressionResistancePriority:UILayoutPriorityDefaultLow
                                               forAxis:UILayoutConstraintAxisHorizontal];
    [row addArrangedSubview:name];
    [row addArrangedSubview:_paths[i]];
    [row addArrangedSubview:_choose[i]];
    [left addArrangedSubview:row];
  }
  _install = [self button:@"Install" action:@selector(install:) filled:YES];
  [left addArrangedSubview:_install];
  _progress = [[UIProgressView alloc] initWithProgressViewStyle:UIProgressViewStyleDefault];
  [left addArrangedSubview:_progress];
  _stage = [self label:@"" size:13 bold:NO];
  [left addArrangedSubview:_stage];

  _log = [[UITextView alloc] init];
  _log.editable = NO;
  _log.font = [UIFont monospacedSystemFontOfSize:10 weight:UIFontWeightRegular];
  _log.backgroundColor = UIColor.secondarySystemBackgroundColor;
  _log.layer.cornerRadius = 8;

  UIStackView* columns = [[UIStackView alloc] initWithArrangedSubviews:@[ left, _log ]];
  columns.axis = UILayoutConstraintAxisHorizontal;
  columns.spacing = 16;
  columns.distribution = UIStackViewDistributionFillProportionally;
  columns.alignment = UIStackViewAlignmentTop;
  columns.translatesAutoresizingMaskIntoConstraints = NO;
  [self.view addSubview:columns];
  UILayoutGuide* g = self.view.safeAreaLayoutGuide;
  [NSLayoutConstraint activateConstraints:@[
    [columns.leadingAnchor constraintEqualToAnchor:g.leadingAnchor constant:16],
    [columns.trailingAnchor constraintEqualToAnchor:g.trailingAnchor constant:-16],
    [columns.topAnchor constraintEqualToAnchor:g.topAnchor constant:12],
    [columns.bottomAnchor constraintLessThanOrEqualToAnchor:g.bottomAnchor constant:-12],
    [_log.widthAnchor constraintEqualToAnchor:left.widthAnchor multiplier:0.6],
    [_log.heightAnchor constraintEqualToAnchor:g.heightAnchor constant:-24],
  ]];
  [self rescan];
}

- (void)rescan {
  _stage.text = @"Looking for your files...";
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
    auto found = std::make_shared<std::array<fs::path, kSlots>>();
    FindPlaced(found->data());
    dispatch_async(dispatch_get_main_queue(), ^{
      for (int i = 0; i < kSlots; i++)
        if (_inputs[i].empty() && !(*found)[i].empty()) _inputs[i] = (*found)[i];
      [self refresh];
      _stage.text = [self ready] ? @"Ready: press Install." : @"Add the missing files, then press Install.";
    });
  });
}

- (bool)ready {
  for (auto& p : _inputs)
    if (p.empty()) return false;
  return true;
}

- (void)refresh {
  for (int i = 0; i < kSlots; i++) {
    _paths[i].text = _inputs[i].empty() ? @"not found" : NS(_inputs[i].filename());
    _paths[i].textColor = _inputs[i].empty() ? UIColor.systemOrangeColor : UIColor.labelColor;
    _choose[i].enabled = !_busy;
  }
  UIButtonConfiguration* cfg = _install.configuration;
  cfg.title = _busy ? @"Cancel" : @"Install";
  _install.configuration = cfg;
  _install.enabled = _busy || [self ready];
}

- (void)choose:(UIButton*)sender {
  _picking = int(sender.tag);
  UIDocumentPickerViewController* picker =
      [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ UTTypeItem, UTTypeData, UTTypeFolder ]
                                                                  asCopy:NO];
  picker.delegate = self;
  picker.allowsMultipleSelection = NO;
  [self presentViewController:picker animated:YES completion:nil];
}

- (void)documentPicker:(UIDocumentPickerViewController*)controller didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
  NSURL* url = urls.firstObject;
  if (!url.isFileURL) return;
  if ([url startAccessingSecurityScopedResource]) {
    if (!g_scoped) g_scoped = [NSMutableArray array];
    [g_scoped addObject:url];
  }
  fs::path p = url.path.UTF8String;
  int want = _picking;
  _stage.text = @"Checking the file...";
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
    int slot = Classify(p);
    dispatch_async(dispatch_get_main_queue(), ^{
      if (slot < 0) {
        _stage.text = [NSString stringWithFormat:@"%s is not a GTA V disc or Title Update 27.", p.filename().c_str()];
        return;
      }
      _inputs[slot] = p;  // in the slot it belongs to, whichever row was tapped
      if (slot != want)
        [self appendLog:[NSString stringWithFormat:@"%s is %s", p.filename().c_str(), kSlotNames[slot]]];
      [self refresh];
      _stage.text = [self ready] ? @"Ready: press Install." : @"Add the missing files, then press Install.";
    });
  });
}

- (void)install:(id)sender {
  if (_busy) {
    if (_ui) _ui->cancelled = true;
    _stage.text = @"Cancelling...";
    return;
  }
  _busy = true;
  [self refresh];
  _ui = std::make_unique<ScreenUi>(self);
  UIApplication.sharedApplication.idleTimerDisabled = YES;
  _bg = [UIApplication.sharedApplication beginBackgroundTaskWithExpirationHandler:^{
    [UIApplication.sharedApplication endBackgroundTask:_bg];
    _bg = UIBackgroundTaskInvalid;
  }];
  loader::InstallInputs in{_inputs[kDisc1].wstring(), _inputs[kDisc2].wstring(), _inputs[kTu].wstring()};
  ScreenUi* ui = _ui.get();
  fs::path expected = fs::path(NSBundle.mainBundle.bundlePath.UTF8String) / "expected.txt";
  std::thread([self, in, ui, expected] {
    std::wstring err;
    bool ok = loader::RunDataInstall(Documents(), expected, in, *ui, &err);
    NSString* e = NS(err);
    dispatch_async(dispatch_get_main_queue(), ^{ [self finished:ok error:e]; });
  }).detach();
}

- (void)finished:(bool)ok error:(NSString*)error {
  _busy = false;
  UIApplication.sharedApplication.idleTimerDisabled = NO;
  if (_bg != UIBackgroundTaskInvalid) [UIApplication.sharedApplication endBackgroundTask:_bg];
  _bg = UIBackgroundTaskInvalid;
  [self refresh];
  if (!ok) {
    _stage.text = _ui && _ui->cancelled ? @"Cancelled. Install again to continue where it stopped."
                                        : [@"Stopped: " stringByAppendingString:error];
    return;
  }
  // Disc images inside the app's folder are no longer needed (~16 GB).
  std::vector<fs::path> placed;
  const std::string docs = Documents().string();
  for (auto& p : _inputs)
    if (p.string().rfind(docs, 0) == 0) placed.push_back(p);
  if (placed.empty()) {
    [self start];
    return;
  }
  UIAlertController* a = [UIAlertController
      alertControllerWithTitle:@"Game data installed"
                       message:@"Delete the disc images and title update from GTA V's folder? They are no longer "
                               @"needed and take about 16 GB."
                preferredStyle:UIAlertControllerStyleAlert];
  [a addAction:[UIAlertAction actionWithTitle:@"Delete"
                                        style:UIAlertActionStyleDestructive
                                      handler:^(UIAlertAction*) {
                                        std::error_code ec;
                                        for (auto& p : placed) fs::remove_all(p, ec);
                                        [self start];
                                      }]];
  [a addAction:[UIAlertAction actionWithTitle:@"Keep" style:UIAlertActionStyleCancel handler:^(UIAlertAction*) {
       [self start];
     }]];
  [self presentViewController:a animated:YES completion:nil];
}

- (void)start {
  _stage.text = @"Starting the game...";
  void (^done)(void) = self.done;
  [self dismissViewControllerAnimated:YES completion:^{
    if (done) done();
  }];
}

- (void)stage:(NSString*)s { _stage.text = s; }
- (void)appendLog:(NSString*)s {
  _log.text = [_log.text stringByAppendingFormat:@"%@\n", s];
  [_log scrollRangeToVisible:NSMakeRange(_log.text.length, 0)];
}
- (void)progress:(double)f { [_progress setProgress:float(f) animated:NO]; }

// From the install thread: a yes/no question, answered on the main thread.
- (BOOL)askBlocking:(NSString*)q {
  dispatch_semaphore_t sem = dispatch_semaphore_create(0);
  __block BOOL yes = NO;
  dispatch_async(dispatch_get_main_queue(), ^{
    UIAlertController* a = [UIAlertController alertControllerWithTitle:@"GTA V"
                                                               message:q
                                                        preferredStyle:UIAlertControllerStyleAlert];
    [a addAction:[UIAlertAction actionWithTitle:@"Continue" style:UIAlertActionStyleDefault handler:^(UIAlertAction*) {
         yes = YES;
         dispatch_semaphore_signal(sem);
       }]];
    [a addAction:[UIAlertAction actionWithTitle:@"Stop" style:UIAlertActionStyleCancel handler:^(UIAlertAction*) {
         dispatch_semaphore_signal(sem);
       }]];
    [self presentViewController:a animated:YES completion:nil];
  });
  dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
  return yes;
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations { return UIInterfaceOrientationMaskLandscape; }
@end

// NSProcessInfo.thermalState for the memory log line (native/capture.cpp): 0 nominal .. 3 critical.
extern "C" int gtav_ios_thermal_state() { return int(NSProcessInfo.processInfo.thermalState); }

namespace gtav::ios_setup {

fs::path DataRoot() { return Documents() / "data"; }

bool DataInstalled() { return loader::IsDataInstalled(Documents()); }

void Show(std::function<void()> done) {
  GtavSetupController* c = [[GtavSetupController alloc] init];
  c.modalPresentationStyle = UIModalPresentationFullScreen;
  auto shared = std::make_shared<std::function<void()>>(std::move(done));
  c.done = ^{ (*shared)(); };
  // The SDL window may not be key yet right after launch: retry until there is a controller.
  __block void (^present)(int) = nil;
  present = ^(int tries) {
    UIViewController* top = TopController();
    if (top) {
      [top presentViewController:c animated:NO completion:nil];
    } else if (tries < 100) {
      dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
        present(tries + 1);
      });
    }
  };
  present(0);
}

}  // namespace gtav::ios_setup
