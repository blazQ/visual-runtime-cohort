import AppKit
import SwiftUI

public struct ContentView: View {
    private let session: VisualRuntimeSession
    @State private var lastPanTranslation: CGSize = .zero
    @State private var viewportSize: CGSize = .zero

    public init(session: VisualRuntimeSession) {
        self.session = session
    }

    public var body: some View {
        ZStack(alignment: .topLeading) {
            MetalView(session: session)
                .gesture(
                    DragGesture(coordinateSpace: .local)
                        .onChanged { value in
                            let delta = CGSize(
                                width: value.translation.width - lastPanTranslation.width,
                                height: value.translation.height - lastPanTranslation.height
                            )
                            lastPanTranslation = value.translation
                            session.panViewBy(x: delta.width, y: delta.height)
                        }
                        .onEnded { _ in
                            lastPanTranslation = .zero
                        }
                )

            KeyHandlingView(session: session)
                .frame(width: 0, height: 0)

            ControlsView(
                backendName: session.backendName,
                onZoomIn: { zoom(by: 1.25) },
                onZoomOut: { zoom(by: 0.8) }
            )
        }
        .onGeometryChange(for: CGSize.self, of: \.size) { newSize in
            viewportSize = newSize
        }
        .frame(minWidth: 800, minHeight: 600)
    }

    private func zoom(by scale: Double) {
        guard viewportSize.width > 0, viewportSize.height > 0 else { return }
        session.zoomViewBy(
            scale: scale,
            anchor: CGPoint(x: viewportSize.width / 2, y: viewportSize.height / 2)
        )
    }
}

private struct KeyHandlingView: NSViewRepresentable {
    let session: VisualRuntimeSession

    func makeNSView(context: Context) -> NSView {
        KeyHandlingNSView(session: session)
    }

    func updateNSView(_ nsView: NSView, context: Context) {}
}

private final class KeyHandlingNSView: NSView {
    private let session: VisualRuntimeSession

    init(session: VisualRuntimeSession) {
        self.session = session
        super.init(frame: .zero)
    }

    required init?(coder: NSCoder) { fatalError() }

    override var acceptsFirstResponder: Bool { true }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        window?.makeFirstResponder(self)
    }

    override func keyDown(with event: NSEvent) {
        switch event.charactersIgnoringModifiers {
        case "q":
            NSApp.terminate(nil)
        case "r":
            session.reload()
        default:
            super.keyDown(with: event)
        }
    }
}
