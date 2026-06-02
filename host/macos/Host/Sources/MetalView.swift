import AppKit
import QuartzCore
import SwiftUI

// MARK: - Backing NSView

final class MetalNSView: NSView {

    override init(frame: NSRect) {
        super.init(frame: frame)
        wantsLayer = true
    }

    required init?(coder: NSCoder) { fatalError() }

    // AppKit calls this once when wantsLayer = true to create the backing layer.
    override func makeBackingLayer() -> CALayer {
        CAMetalLayer()
    }

    override var wantsUpdateLayer: Bool { true }

    var drawableSizeDidChange: ((VisualRuntimeSurfaceMetrics) -> Void)?
    var scrollDidChange: ((Double, CGPoint) -> Void)?
    private var lastDrawableScale: CGFloat = 0
    private var lastDrawableSize: CGSize = .zero
    private var lastScreenSize: CGSize = .zero

    var metalLayer: CAMetalLayer { layer as! CAMetalLayer }

    override func setFrameSize(_ newSize: NSSize) {
        super.setFrameSize(newSize)
        updateDrawableSize(newSize)
    }

    override func viewDidChangeBackingProperties() {
        super.viewDidChangeBackingProperties()
        updateDrawableSize(frame.size)
    }

    func updateDrawableSize(_ size: NSSize) {
        let scale = window?.backingScaleFactor ?? 1.0
        let drawableSize = CGSize(width: size.width * scale, height: size.height * scale)
        guard scale != lastDrawableScale ||
              drawableSize != lastDrawableSize ||
              size != lastScreenSize else { return }

        lastDrawableScale = scale
        lastDrawableSize = drawableSize
        lastScreenSize = size
        metalLayer.contentsScale = scale
        metalLayer.drawableSize = drawableSize
        drawableSizeDidChange?(metalLayer.visualRuntimeSurfaceMetrics)
    }

    override func scrollWheel(with event: NSEvent) {
        let localPoint = convert(event.locationInWindow, from: nil)
        let screenPoint = CGPoint(x: localPoint.x, y: bounds.height - localPoint.y)
        scrollDidChange?(event.scrollingDeltaY, screenPoint)
    }
}

// MARK: - SwiftUI wrapper

struct MetalView: NSViewRepresentable {
    let session: VisualRuntimeSession

    func makeCoordinator() -> Coordinator { Coordinator(session: session) }

    func makeNSView(context: Context) -> MetalNSView {
        let view = MetalNSView()
        context.coordinator.start(view: view)
        return view
    }

    func updateNSView(_ view: MetalNSView, context: Context) {}

    // MARK: Coordinator - owns the display link and drives visual runtime ticks

    final class Coordinator: NSObject {
        let session: VisualRuntimeSession
        private var displayLink: CADisplayLink?
        private var lastTime: Double = 0
        private var pendingScrollZoom: Double = 0
        private var scrollZoomAnchor: CGPoint = .zero

        private let scrollZoomSpeed = 0.018
        private let scrollZoomTimeConstant = 0.04
        private let minimumPendingScrollZoom = 0.00001

        init(session: VisualRuntimeSession) { self.session = session }

        func start(view: MetalNSView) {
            view.updateDrawableSize(view.frame.size)
            view.drawableSizeDidChange = { [weak session] metrics in
                session?.resize(metrics)
            }
            view.scrollDidChange = { [weak self] deltaY, anchor in
                self?.enqueueScrollZoom(deltaY: deltaY, anchor: anchor)
            }
            session.attach(view.metalLayer)

            let displayLink = view.displayLink(target: self, selector: #selector(displayLinkFired(_:)))
            displayLink.add(to: .main, forMode: .common)
            self.displayLink = displayLink
        }

        private func enqueueScrollZoom(deltaY: Double, anchor: CGPoint) {
            guard deltaY.isFinite else { return }
            pendingScrollZoom += deltaY * scrollZoomSpeed
            scrollZoomAnchor = anchor
        }

        @objc private func displayLinkFired(_ displayLink: CADisplayLink) {
            let now = displayLink.timestamp
            let dt = lastTime == 0 ? 0.0 : now - lastTime
            lastTime = now

            if dt > 0, abs(pendingScrollZoom) > minimumPendingScrollZoom {
                let blend = 1.0 - exp(-dt / scrollZoomTimeConstant)
                let zoomStep = pendingScrollZoom * blend
                pendingScrollZoom -= zoomStep
                session.zoomViewBy(logScale: zoomStep, anchor: scrollZoomAnchor)
            } else if abs(pendingScrollZoom) <= minimumPendingScrollZoom {
                pendingScrollZoom = 0
            }

            session.tick(Float(dt))
        }

        deinit { displayLink?.invalidate() }
    }
}
