import SwiftUI

struct WorkspaceView: View {
    let session: VisualRuntimeSession
    let sceneModel: SceneModel
    let sceneSettings: VisualRuntimeSession.SceneSettings
    @Binding var backgroundColor: Color
    @State private var viewportSize: CGSize = .zero
    @State private var activeStagedShapeID: SceneShape.ID?

    var body: some View {
        ZStack(alignment: .topLeading) {
            CanvasView(
                session: session,
                sceneSettings: sceneSettings,
                isPlacing: activeStagedShapeID != nil,
                onPointerMove: handlePointerMove,
                onPointerClick: handlePointerClick
            )

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
        guard activeStagedShapeID == nil else { return }

        switch item {
        case let .rectangle(rectangle):
            let shape = sceneModel.stageRectangle(rectangle)
            activeStagedShapeID = shape.id
            session.upsertShape(shape)
        }
    }

    private func handlePointerMove(_ screenPoint: CGPoint) {
        guard let id = activeStagedShapeID,
              let worldPoint = session.screenToWorld(screenPoint),
              let shape = sceneModel.updateStagedShape(id: id, center: worldPoint)
        else { return }

        session.upsertShape(shape)
    }

    private func handlePointerClick(_ screenPoint: CGPoint) {
        handlePointerMove(screenPoint)
        guard let id = activeStagedShapeID else { return }

        _ = sceneModel.commitStagedShape(id: id)
        activeStagedShapeID = nil
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
