Nori
---

This is a **very early** work-in-progress GUI toolkit. Don't expect much yet; it's still a mess.

- Wayland only. X11 or any other platform support is not currently planned.
- Vulkan is used for rendering. Maybe GLES2 and/or software rendering some day.
- Retained mode renderer.
- Focus on efficiency with proper damage tracking and frame reuse.
- No massive platform abstraction layers.
- No Glib at all; no dependencies that have a hard dependency on Glib will be considered.
- I still have no idea what it's going to look like or how the API is going to be structured.
