#ifndef MACWIRE_LIST_POSITION_H
#define MACWIRE_LIST_POSITION_H
void LoadListPosition(void);
void ValidateSavedFeed(void);
void CancelListPositionRestore(void);
/* Called after sorting the first successfully downloaded index. */
int RestoreListPosition(void);
int SaveListPosition(void);
#endif
