import CoreGraphics
import Darwin
import QuartzCore

public final class VisualRuntimeSession {
    private var host: VisualRuntimeHost
    public var backendName: String {
        String(host.backendName())
    }

    public init?(libPath: String) {
        let host = VisualRuntimeHost(std.string(libPath))
        guard host.valid() else {
            return nil
        }
        self.host = host
    }

    func attach(_ layer: CAMetalLayer) {
        host.attachSurface(
            Unmanaged.passUnretained(layer).toOpaque(),
            layer.visualRuntimeSurfaceMetrics
        )
    }

    func resize(_ metrics: VisualRuntimeSurfaceMetrics) {
        host.resize(metrics)
    }

    func changeView(_ build: (inout ViewChange) -> Void) {
        var change = ViewChange()
        build(&change)
        guard !change.isEmpty else { return }
        host.changeView(change.rawValue)
    }

    func panViewBy(x: Double, y: Double) {
        changeView { change in
            change.panBy(x: x, y: y)
        }
    }

    func zoomViewBy(scale: Double, anchor: CGPoint) {
        changeView { change in
            change.zoomBy(scale: scale, anchor: anchor)
        }
    }

    func zoomViewBy(logScale: Double, anchor: CGPoint) {
        changeView { change in
            change.zoomBy(logScale: logScale, anchor: anchor)
        }
    }

    func tick(_ dt: Float) {
        host.tick(dt)
    }

    func reload() {
        _ = host.reload()
    }
}

extension VisualRuntimeSession {
    struct ViewChange {
        fileprivate private(set) var rawValue = VisualRuntimeViewChange(
            flags: VisualRuntimeViewChange_None.rawValue,
            reserved: 0,
            pan_x_screen: 0,
            pan_y_screen: 0,
            zoom_delta_log_scale: 0,
            zoom_anchor_x_screen: 0,
            zoom_anchor_y_screen: 0
        )

        fileprivate var isEmpty: Bool {
            rawValue.flags == VisualRuntimeViewChange_None.rawValue
        }

        mutating func panBy(x: Double, y: Double) {
            guard x.isFinite, y.isFinite, x != 0 || y != 0 else { return }
            rawValue.flags |= VisualRuntimeViewChange_Pan.rawValue
            rawValue.pan_x_screen += x
            rawValue.pan_y_screen += y
        }

        mutating func zoomBy(scale: Double, anchor: CGPoint) {
            guard scale.isFinite, scale > 0 else { return }
            zoomBy(logScale: log1p(scale - 1), anchor: anchor)
        }

        mutating func zoomBy(logScale: Double, anchor: CGPoint) {
            guard logScale.isFinite,
                  anchor.x.isFinite,
                  anchor.y.isFinite else { return }
            rawValue.flags |= VisualRuntimeViewChange_Zoom.rawValue
            rawValue.zoom_delta_log_scale += logScale
            rawValue.zoom_anchor_x_screen = anchor.x
            rawValue.zoom_anchor_y_screen = anchor.y
        }
    }
}

extension CAMetalLayer {
    var visualRuntimeSurfaceMetrics: VisualRuntimeSurfaceMetrics {
        VisualRuntimeSurfaceMetrics(
            pixel_width: UInt32(drawableSize.width),
            pixel_height: UInt32(drawableSize.height),
            screen_width: bounds.width,
            screen_height: bounds.height
        )
    }
}
