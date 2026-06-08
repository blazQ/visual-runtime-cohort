# Visual Runtime Cohort Harness

This context describes the language for the cohort harness and visual runtime boundary.

## Language

**Surface**:
The native drawing destination offered by the harness to the visual runtime. A surface may wrap platform-specific windowing objects, but the visual runtime boundary should name it as a surface rather than as a specific graphics API object.
_Avoid_: metal layer, drawable, window

**Platform Surface Handle**:
The platform-native object carried inside a surface so the visual runtime can bind its compiled renderer backend to the harness-owned drawing destination.
_Avoid_: graphics context, renderer-owned window

**Linux Surface Variant**:
An explicit Linux platform surface shape used by the Linux harness when offering a drawing destination to the visual runtime. Wayland and X11 are separate variants rather than a single vague Linux window concept.
_Avoid_: generic Linux native window, universal desktop surface

**Linux XCB Window Surface**:
The first implemented Linux surface variant. The harness passes an XCB connection and XCB window identifier for an X11 window so the visual runtime's Vulkan backend can create the Vulkan surface.
_Avoid_: generic X11 handle, Xlib surface, GLFW-created Vulkan surface

**Linux Wayland Surface**:
A Linux surface variant for native Wayland sessions. The harness passes a Wayland display and Wayland surface so the visual runtime's Vulkan backend can create the Vulkan surface.
_Avoid_: Wayland mode, GLFW-created Vulkan surface, generic Linux window

**Harness**:
The application shell that owns the process, window, run loop, and visual runtime dylib lifecycle.
_Avoid_: app layer, host app

**Linux Harness**:
A harness for modern Linux desktop environments that loads the same visual runtime boundary as the macOS harness while owning Linux-specific window and event-loop concerns.
_Avoid_: portable runtime window layer, Linux renderer

**GLFW Minimal Harness**:
The first Linux-oriented harness, located under `host/glfw-minimal`, that uses GLFW only to provide a window, event loop, resize notifications, and platform surface handles for the visual runtime. It is not a cross-platform UI framework or a replacement for the macOS harness.
_Avoid_: portable app shell, GLFW renderer, UI parity harness

**Visual Runtime**:
The reloadable dylib that owns visual behavior behind the visual runtime API boundary. Use this as the user-facing top-level concept instead of the generic term runtime when discussing rendering behavior.
_Avoid_: renderer app, host, generic runtime

**Visual Runtime API Boundary**:
The narrow C-compatible contract shared by the harness and the visual runtime.
_Avoid_: renderer API, Metal API

**Renderer Backend**:
The graphics API implementation compiled into the visual runtime for a given build. Renderer backend choice is a build-time concern, not a harness runtime concern.
_Avoid_: runtime renderer mode, host backend

**Visual Runtime API**:
The product-shaped interface of the visual runtime for creating and updating visual state. It uses product terminology rather than generic renderer concepts unless the product itself needs those concepts. Callers use the API without knowing whether or how the runtime retains visual state internally. In the current harness, this API may begin as an internal runtime seam before being exposed across the visual runtime API boundary.
_Avoid_: renderer API, RHI, backend API, premature mesh/material/object API

**Shape Upsert**:
A product-shaped request to create or replace scene shape content with a caller-chosen identity. Shape upserts describe the intended visual content in product terms and do not expose how the visual runtime stores or draws that content.
_Avoid_: draw command, renderer object, GPU resource update

**World Unit**:
A scene-space unit used to describe retained visual content independently from the current surface size, pixel density, or camera/view. World units are not screen units, physical pixels, or normalized device coordinates.
_Avoid_: screen point, framebuffer pixel, clip-space coordinate

**World-Space Rectangle**:
An axis-aligned rectangle in world units, described by its center position and size. It belongs to the scene rather than to the current viewport.
_Avoid_: screen-space rectangle, viewport overlay, pixel bounds

**World Transform**:
A shape-owned transform that maps the shape's local geometry into world space. For simple shapes, the world transform may carry placement and size while the local geometry stays canonical.
_Avoid_: local transform, baked vertex position, viewport transform

**Canonical Rectangle Geometry**:
The reusable local-space rectangle shape before placement, sizing, or view projection is applied. Future stable local attributes, such as default texture coordinates, belong to this geometry rather than to the world transform.
_Avoid_: pre-positioned rectangle vertices, per-instance rectangle mesh, world-space quad

**View Intent Delta**:
An already-interpreted visual change request passed to the visual runtime, such as a pan or zoom delta. Product and input logic decide the delta; the visual runtime applies it to its retained visual intention without owning raw input interpretation, acceleration, or smoothing policy.
_Avoid_: raw gesture, input event, animation policy, smoothing command

**Screen Unit**:
A display-independent screen-space unit used for visual interaction deltas and anchors at the visual runtime boundary. On macOS this corresponds to points; on other harnesses it should correspond to logical pixels or an equivalent display-independent unit rather than physical framebuffer pixels.
_Avoid_: physical pixel, framebuffer pixel, viewport ratio, world unit

**Surface Metrics**:
The paired drawable-pixel size and display-independent screen-unit size for a visual runtime surface. Pixel size drives graphics API drawable or swapchain sizing; screen size drives product-shaped interaction deltas and anchors.
_Avoid_: raw framebuffer size, viewport-only size, mixed pixel/logical dimensions

**Zoom Intent Delta**:
A relative zoom change expressed as a logarithmic scale delta, anchored at a screen-space point. Positive values zoom in, negative values zoom out, and zero is a no-op. It is not a bounded or normalized absolute zoom level.
_Avoid_: zoom level, normalized zoom, min/max zoom policy, raw wheel delta

**View Change**:
A product-shaped request to change the current view intention, such as panning and zooming together in one atomic interaction step. A view change is not a raw input event and should not encode input-device policy.
_Avoid_: input event, camera command, renderer command, raw gesture

**Scene Settings**:
Product-shaped presentation preferences owned by the harness and supplied to the visual runtime as an absolute snapshot for the scene, such as the scene background color. Scene settings describe colors as linear RGBA values and are not renderer commands, backend options, or per-frame graphics descriptors.
_Avoid_: renderer settings, render settings, generic parameters, user properties

**Linear RGBA Color**:
A semantic color value with red, green, blue, and alpha channels expressed in linear color space. At the visual runtime API boundary, color values should be named as colors rather than as generic four-float vectors.
_Avoid_: generic float4, SIMD vector, GLM vector

**Scene Settings Default**:
The visual runtime's fallback scene settings used before a harness supplies its app-owned scene settings snapshot.
_Avoid_: backend default, host-only default

**Visual Runtime Feature Parity**:
The expectation that a participant can work on the same visual-runtime behavior across supported harnesses, even when each harness has different native UI affordances.
_Avoid_: harness UI parity, identical app shell

**Render World**:
The visual runtime's internal retained visual state derived from Visual Runtime API calls and consumed by renderer backends each frame. The render world is not exposed to the harness as a data structure and should not force product-facing concepts before product features require them.
_Avoid_: product scene, harness world, backend state

**Drawable Instance**:
Renderer-owned state for one retained visual item. A drawable instance may cache geometry, transform, color, or backend upload state, but it should not prevent the visual runtime from sharing canonical geometry or other reusable resources as the renderer model matures.
_Avoid_: public scene object, permanent unique mesh, harness-owned resource

**Drawable Instance State**:
The renderer-facing state for one drawable instance, derived from the render world and updated without changing canonical geometry. For rectangles, instance state includes the world transform and color.
_Avoid_: baked vertex attributes, product shape descriptor, backend-only cache

**Frame Config**:
Product-shaped frame-level visual settings used by renderer backends, such as clear color or current view presentation. Frame config is the visual runtime's canonical frame intent; backend-specific shader uniforms or upload caches are implementation details derived from it. Its expected home is the runtime partition, not a renderer backend partition.
_Avoid_: pipeline config, render pass descriptor, backend settings

**Runtime Partition**:
The shared visual-runtime area for product-shaped runtime concepts, such as frame config, that are not specific to Metal or Vulkan. Runtime partition code should not become a graphics abstraction layer.
_Avoid_: RHI folder, shared backend layer, generic runtime layer

**Backend Partition**:
A backend-named area that contains graphics API-specific renderer code or shader assets. Shared visual runtime entry points should stay outside backend partitions.
_Avoid_: mixed renderer file, flat shader bucket

## Example Dialogue

Dev: "The harness has a surface ready for the visual runtime."

Domain expert: "Good. The surface may be backed by a platform object, but the visual runtime API boundary should not call it a Metal layer."

Dev: "Should the harness pass a Vulkan instance?"

Domain expert: "No. The harness passes a platform surface handle; the visual runtime owns graphics API objects."

Dev: "Should the harness choose Vulkan or Metal?"

Domain expert: "No. The visual runtime is built with one renderer backend, and the harness only loads the runtime."

Dev: "Where should the Vulkan shaders go?"

Domain expert: "In the Vulkan backend partition. Metal and Vulkan assets should not be mixed in one flat shader directory."

Dev: "Where should clear color live if both Metal and Vulkan need it?"

Domain expert: "If it expresses product-shaped frame intent, put it in the runtime partition as frame config. Do not create a generic renderer abstraction just to deduplicate backend code."

Dev: "Should the harness call this renderer settings?"

Domain expert: "No. The harness supplies scene settings, and the visual runtime derives frame config and backend details from them."

Dev: "Should the Linux harness match the macOS harness UI?"

Domain expert: "No. The first Linux target is visual runtime feature parity, not identical native harness UI."

Dev: "Can the Linux harness pass one generic Linux window handle?"

Domain expert: "No. The surface should name the Linux surface variant explicitly, such as Wayland or X11, so the visual runtime can bind the correct Vulkan surface path."
