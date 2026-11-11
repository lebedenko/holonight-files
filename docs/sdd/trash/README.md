# Trash in Files

**Move to Trash** accepts user-owned private directories with mode `0700` or
`2700`. New directories request `0700`. The NFS fallback reserves a unique
sidecar, checks the payload name through the private directory descriptor and
uses an ordinary rename. After an ambiguous NFS response, Files checks both
locations and retains the sidecar if the move remains uncertain. Existing
directories stay under the user's control.

Open **Trash** in Places to see top-level Trash items across available volumes,
including their original locations and deletion dates. Scan problems are shown
alongside the listing. Restore uses the original path. If the folder is missing
or the name is already in use, choose an existing folder and a name under
**Restore To…**. Restore preserves existing destination files.

**Delete Permanently** and **Empty Trash** require confirmation. Empty Trash
uses the entries shown when the confirmation opens. Mutations run through the
Files task worker. The footer reports per-item failures and the view refreshes.
