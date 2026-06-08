import SwiftUI

struct WorkspaceView: View {
    let session: VisualRuntimeSession
    let sceneModel: SceneModel
    let sceneSettings: VisualRuntimeSession.SceneSettings
    @Binding var backgroundColor: Color
    @State private var viewportSize: CGSize = .zero

    var body: some View {
        ZStack(alignment: .topLeading) {
            CanvasView(session: session, sceneSettings: sceneSettings)

            KeyHandlingView(session: session)
                .frame(width: 0, height: 0)

            ControlsView(
                backendName: session.backendName,
                backgroundColor: $backgroundColor,
                onAction: handleAction
            )
        }
        .onGeometryChange(for: CGSize.self, of: \.size) { newSize in
            viewportSize = newSize
        }
    }

    private func zoom(by scale: Double) {
        guard viewportSize.width > 0, viewportSize.height > 0 else { return }
        session.zoomViewBy(
            scale: scale,
            anchor: CGPoint(x: viewportSize.width / 2, y: viewportSize.height / 2)
        )
    }

    private func handleAction(_ action: WorkspaceAction) {
        switch action {
        case let .addCanvasItem(item):
            addCanvasItem(item)
        case .zoomIn:
            zoom(by: 1.25)
        case .zoomOut:
            zoom(by: 0.8)
        }
    }

    private func addCanvasItem(_ item: CanvasItem) {
        switch item {
        case let .rectangle(rectangle):
            session.upsertShape(sceneModel.addRectangle(rectangle))
        }
    }
}

#Preview("Workspace") {
    @Previewable @State var backgroundColor = AppSettings.defaultSceneBackgroundColor

    previewWithSession { session in
        WorkspaceView(
            session: session,
            sceneModel: SceneModel(),
            sceneSettings: VisualRuntimeSession.SceneSettings(backgroundColor: backgroundColor),
            backgroundColor: $backgroundColor
        )
        .frame(width: 800, height: 600)
    }
}
