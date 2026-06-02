import SwiftUI

struct ControlsView: View {
    let backendName: String
    let onZoomIn: () -> Void
    let onZoomOut: () -> Void

    var body: some View {
        ZStack {
            StatusView(backendName: backendName)
                .padding(12)
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)

            ZoomControls(
                onZoomIn: onZoomIn,
                onZoomOut: onZoomOut
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
            onZoomIn: {},
            onZoomOut: {}
        )
    }
    .frame(width: 800, height: 600)
}
