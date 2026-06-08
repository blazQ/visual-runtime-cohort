import Foundation
import SwiftUI

@ViewBuilder
func previewWithSession<Content: View>(
    @ViewBuilder content: (VisualRuntimeSession) -> Content
) -> some View {
    if let session = VisualRuntimeSession.previewSession {
        content(session)
    } else {
        Text("Build the visual runtime dylib to enable this preview.")
            .frame(width: 420, height: 240)
    }
}

private extension VisualRuntimeSession {
    static var previewSession: VisualRuntimeSession? {
        if let path = Bundle.main.infoDictionary?["VisualRuntimeLibPath"] as? String,
           !path.isEmpty,
           let session = VisualRuntimeSession(libPath: path) {
            return session
        }

        return VisualRuntimeSession(libPath: previewLibraryPath)
    }

    static var previewLibraryPath: String {
        let sourcesDirectory = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
        let repoRoot = sourcesDirectory
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .deletingLastPathComponent()

        return repoRoot
            .appendingPathComponent("build/lib/libvisual_runtime.dylib")
            .path
    }
}
