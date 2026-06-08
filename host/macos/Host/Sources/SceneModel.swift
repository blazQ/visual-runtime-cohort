import Foundation
import SwiftUI

@MainActor
@Observable
final class SceneModel {
    private var nextShapeID: SceneShape.ID = 1
    private(set) var shapes: [SceneShape] = []

    func addRectangle(_ rectangle: RectangleShape) -> SceneShape {
        let shape = SceneShape(
            id: nextShapeID,
            kind: .rectangle,
            center: rectangle.center,
            size: rectangle.size,
            color: rectangle.color
        )
        nextShapeID += 1
        shapes.append(shape)
        return shape
    }
}

struct SceneShape: Identifiable {
    typealias ID = UInt64

    let id: ID
    let kind: SceneShapeKind
    let center: CGPoint
    let size: CGSize
    let color: Color
}

enum SceneShapeKind {
    case rectangle
}
