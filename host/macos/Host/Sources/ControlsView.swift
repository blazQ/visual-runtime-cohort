import SwiftUI

struct ControlsView: View {
    let backendName: String
    @Binding var backgroundColor: Color
    let onAction: (WorkspaceAction) -> Void

    var body: some View {
        ZStack {
            StatusView(backendName: backendName)
                .padding(12)
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)

            ShapeToolbar(backgroundColor: $backgroundColor, onAction: onAction)
                .padding(16)
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottom)

            ZoomControls(
                onZoomIn: { onAction(.zoomIn) },
                onZoomOut: { onAction(.zoomOut) }
            )
            .padding(16)
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottomTrailing)
        }
    }
}

#Preview("Controls") {
    ZStack {
        Rectangle()
            .fill(.black.gradient)

        ControlsView(
            backendName: "preview",
            backgroundColor: .constant(.black),
            onAction: { _ in }
        )
    }
    .frame(width: 800, height: 600)
}
