import SwiftUI

struct CanvasView: View {
    let session: VisualRuntimeSession
    let sceneSettings: VisualRuntimeSession.SceneSettings
    let isPlacing: Bool
    let onPointerMove: (CGPoint) -> Void
    let onPointerClick: (CGPoint) -> Void
    @State private var lastPanTranslation: CGSize = .zero

    var body: some View {
        MetalView(
            session: session,
            sceneSettings: sceneSettings,
            onPointerMove: isPlacing ? onPointerMove : nil,
            onPointerClick: isPlacing ? onPointerClick : nil
        )
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .gesture(panGesture)
    }

    private var panGesture: some Gesture {
        DragGesture(coordinateSpace: .local)
            .onChanged { value in
                guard !isPlacing else { return }

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
    }
}

#Preview("Canvas") {
    previewWithSession { session in
        CanvasView(
            session: session,
            sceneSettings: VisualRuntimeSession.SceneSettings(backgroundColor: .black),
            isPlacing: false,
            onPointerMove: { _ in },
            onPointerClick: { _ in }
        )
        .frame(width: 800, height: 600)
    }
}
