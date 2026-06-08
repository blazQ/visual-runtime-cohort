import SwiftUI

public struct ContentView: View {
    private let session: VisualRuntimeSession
    @State private var appSettings = AppSettings()
    @State private var sceneModel = SceneModel()

    public init(session: VisualRuntimeSession) {
        self.session = session
    }

    public var body: some View {
        WorkspaceView(
            session: session,
            sceneModel: sceneModel,
            sceneSettings: sceneSettings,
            backgroundColor: $appSettings.sceneBackgroundColor
        )
        .frame(minWidth: 800, minHeight: 600)
    }

    private var sceneSettings: VisualRuntimeSession.SceneSettings {
        VisualRuntimeSession.SceneSettings(backgroundColor: appSettings.sceneBackgroundColor)
    }
}

#Preview("Content") {
    previewWithSession { session in
        ContentView(session: session)
    }
}
