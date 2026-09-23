# Design

A unique hnunknownsize QImageIOPlugin recognizes a binary signature, advertises
CanRead, and reports invalid QSize. Its read method records a marker and returns
small pixels; a direct Qt positive control proves both the handler and detector work.
The fixture uses a .png filename only to enter Files' existing image MIME routing;
the installed Images provider detects the unique synthetic content format.

A separate executable adds its private plugin directory at runtime. No global
QT_PLUGIN_PATH or install rule is added. Tests call the installed Images library,
ThumbnailService (automatic and explicit tiers), and asynchronous PreviewService.
Temporary cache and marker paths isolate writes. Real valid PNG selection verifies
recovery; the existing cancellation presentation mapping verifies silence.
