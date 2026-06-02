import SwiftUI

struct StatusView: View {
    let backendName: String

    var body: some View {
        VStack(spacing: 4) {
            Text("drag to pan, scroll to zoom, q to close, r to reload")
            Text("backend: \(backendName)")
        }
        .fontDesign(.rounded)
        .bold()
        .foregroundStyle(.white)
        .multilineTextAlignment(.center)
        .shadow(color: .black.opacity(0.8), radius: 2, x: 0, y: 1)
        .allowsHitTesting(false)
    }
}

#Preview("Status") {
    ZStack(alignment: .top) {
        Rectangle()
            .fill(.black.gradient)

        StatusView(backendName: "preview")
            .padding(12)
    }
    .frame(width: 800, height: 200)
}
