import SwiftUI

struct MainToolbar: View {
    @Binding var backgroundColor: Color
    let onAction: (WorkspaceAction) -> Void

    var body: some View {
        HStack(spacing: 12) {
            ColorPicker("Background", selection: $backgroundColor, supportsOpacity: true)
                .labelsHidden()
                .accessibilityLabel("Background color")

            Divider()
                .frame(height: 28)

            shapeButton("Add Rectangle", systemImage: "rectangle") {
                onAction(
                    .addCanvasItem(
                        .rectangle(
                            RectangleShape(
                                center: .zero,
                                size: CGSize(width: 2, height: 1),
                                color: .orange
                            )
                        )
                    )
                )
            }
        }
        .padding(10)
        .compatGlassEffect(in: .capsule)
    }

    private func shapeButton(
        _ title: LocalizedStringKey,
        systemImage: String,
        action: @escaping () -> Void
    ) -> some View {
        Button(action: action) {
            Label(title, systemImage: systemImage)
                .labelStyle(.iconOnly)
                .frame(width: 28, height: 28)
                .contentShape(Rectangle())
        }
        .compatGlassButtonStyle()
        .controlSize(.large)
        .buttonBorderShape(.circle)
    }
}

#Preview("Shape Toolbar") {
    ZStack {
        Rectangle()
            .fill(.black.gradient)

        MainToolbar(backgroundColor: .constant(.black), onAction: { _ in })
    }
    .frame(width: 420, height: 160)
}
