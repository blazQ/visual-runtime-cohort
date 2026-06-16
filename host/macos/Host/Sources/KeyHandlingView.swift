import AppKit
import SwiftUI

struct KeyHandlingView: NSViewRepresentable {
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

#Preview("Key Handling") {
    previewWithSession { session in
        KeyHandlingView(session: session)
            .frame(width: 40, height: 40)
    }
}
