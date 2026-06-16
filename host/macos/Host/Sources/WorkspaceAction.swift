import SwiftUI

enum WorkspaceAction {
    case addCanvasItem(CanvasItem)
    case zoomIn
    case zoomOut
}

enum CanvasItem {
    case rectangle(RectangleShape)
}

struct RectangleShape {
    let center: CGPoint
    let size: CGSize
    let color: Color
}
