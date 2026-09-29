# Design

PreviewPane retains the complete candidate chain of its displayed fallback and a separate
validity flag (an empty chain is a valid placeholder identity). Matching navigation preserves
the shared visibility predicate for theme, bundled and question-mark tiers. Image arrival,
clear, identity change and effective hiding invalidate it. Metadata stays bound to the service.

PreviewService keeps inspection busy/error and Quick Look busy/error separate. Eligibility
is explicitly Checking, Supported or Unsupported; hasText and textLines describe loaded
content only. The existing routing gate accepts Checking. Text eligibility also selects the
text frame before content arrives; error states use the compact frame.

The persistent worker performs verified nonblocking regular-file opening and bounded MIME
sniffing for inspection or requested text loading. Inspection returns metadata and image/EXIF
without calling readHead. Text requests reopen and verify the descriptor, sniff and enforce
supported text types, and then use the existing bounded text reader. Text requests carry
inspection generation, selection revision and a monotonic Quick Look request identity.
A separate cancellation token permits closing text without cancelling sidebar work. No text
cache is introduced. Refresh invalidates and reloads active text, retaining the current line
where possible; reopening resets the line to zero. Test hooks block inspection or text before
filesystem work to verify race handling without filesystem timing assumptions.
