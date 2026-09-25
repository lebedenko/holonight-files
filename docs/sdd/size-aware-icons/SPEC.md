# Files explicit icon rendering requirements

Status: Approved by the supplied implementation plan.

- When a Files view requests its own `image://icon/` provider, it shall pass that URL to `HnIcon` with `Original` rendering so the existing theme artwork and fallback chain remain unchanged.
- When a Files view shows a bundled fallback SVG, it shall use `Semantic` rendering so its foreground follows the component palette.
- This cycle shall not change Files' sidebar icon names or file-view presentation.
