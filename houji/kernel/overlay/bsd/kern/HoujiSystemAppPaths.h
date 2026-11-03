/* SPDX-License-Identifier: MIT
 * Original 20H392 system-app bundles restored by tools/restore-system-apps.py.
 * Only a canonical executable path beneath one of these exact bundles qualifies.
 */
#ifndef HOUJI_SYSTEM_APP_PATHS_H
#define HOUJI_SYSTEM_APP_PATHS_H
static int houji_system_app_path(const char *path) {
 static const char *const bundles[] = {
  "/Applications/AppleTV.app/",
  "/Applications/Books.app/",
  "/Applications/Bridge.app/",
  "/Applications/Calculator.app/",
  "/Applications/Compass.app/",
  "/Applications/Contacts.app/",
  "/Applications/FaceTime.app/",
  "/Applications/Files.app/",
  "/Applications/FindMy.app/",
  "/Applications/Fitness.app/",
  "/Applications/Freeform.app/",
  "/Applications/Health.app/",
  "/Applications/Home.app/",
  "/Applications/Magnifier.app/",
  "/Applications/Maps.app/",
  "/Applications/Measure.app/",
  "/Applications/MobileCal.app/",
  "/Applications/MobileMail.app/",
  "/Applications/MobileNotes.app/",
  "/Applications/MobileStore.app/",
  "/Applications/MobileTimer.app/",
  "/Applications/Music.app/",
  "/Applications/News.app/",
  "/Applications/Passbook.app/",
  "/Applications/Podcasts.app/",
  "/Applications/Reminders.app/",
  "/Applications/SequoiaTranslator.app/",
  "/Applications/Shortcuts.app/",
  "/Applications/Stocks.app/",
  "/Applications/Tips.app/",
  "/Applications/VoiceMemos.app/",
  "/Applications/Weather.app/",
 };
 if (!path || path[0]!='/') return 0;
 const char *component=path+1;
 for (;;) {
  const char *end=component;
  while (*end && *end!='/') ++end;
  size_t n=(size_t)(end-component);
  if (!n || (n==1 && component[0]=='.') ||
      (n==2 && component[0]=='.' && component[1]=='.')) return 0;
  if (!*end) break;
  component=end+1;
 }
 for (unsigned i=0;i<sizeof(bundles)/sizeof(bundles[0]);++i) {
  size_t n=strlen(bundles[i]);
  if (!strncmp(path,bundles[i],n) && path[n] && path[strlen(path)-1]!='/') return 1;
 }
 return 0;
}
#endif
