/* tinyfiledialogs.c shim - GUI file dialogs are a Phase-1 non-goal (no
 * window system on BMX). dos_programs.cpp uses them for image-browse
 * buttons; returning NULL makes those paths report "no file" safely. */

static const char *tinyfd_openFileDialog(const char *aTitle,
    const char *aDefaultPathAndFile, int aNumOfFilterPatterns,
    const char * const *aFilterPatterns, const char *aSingleFilterDescription,
    int aAllowMultipleSelects) {
  (void)aTitle; (void)aDefaultPathAndFile; (void)aNumOfFilterPatterns;
  (void)aFilterPatterns; (void)aSingleFilterDescription;
  (void)aAllowMultipleSelects;
  return 0;
}

static int tinyfd_messageBox(const char *aTitle, const char *aMessage,
    const char *aDialogType, const char *aIconType, int aDefaultButton) {
  (void)aTitle; (void)aMessage; (void)aDialogType; (void)aIconType;
  (void)aDefaultButton;
  return 0;
}

static const char *tinyfd_selectFolderDialog(const char *aTitle,
    const char *aDefaultPath) {
  (void)aTitle; (void)aDefaultPath;
  return 0;
}
