import SwiftUI

struct CanvasView: View {
    let session: VisualRuntimeSession
    let sceneSettings: VisualRuntimeSession.SceneSettings
    @State private var lastPanTranslation: CGSize = .zero

    var body: some View {
        MetalView(session: session, sceneSettings: sceneSettings)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .gesture(panGesture)
    }

    private var panGesture: some Gesture {
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
    }
}

#Preview("Canvas") {
    previewWithSession { session in
        CanvasView(
            session: session,
            sceneSettings: VisualRuntimeSession.SceneSettings(backgroundColor: .black)
        )
        .frame(width: 800, height: 600)
    }
}
