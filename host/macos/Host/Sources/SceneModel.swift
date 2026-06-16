import Foundation
import SwiftUI

@MainActor
@Observable
final class SceneModel {
    private var nextShapeID: SceneShape.ID = 1
    private(set) var shapes: [SceneShape] = []
    private(set) var stagedShapes: [SceneShape] = []

    func addRectangle(_ rectangle: RectangleShape) -> SceneShape {
        let shape = makeShape(kind: .rectangle, rectangle: rectangle)
        shapes.append(shape)
        return shape
    }

    func stageRectangle(_ rectangle: RectangleShape) -> SceneShape {
        let shape = makeShape(kind: .rectangle, rectangle: rectangle)
        stagedShapes.append(shape)
        return shape
    }

    func updateStagedShape(id: SceneShape.ID, center: CGPoint) -> SceneShape? {
        guard let index = stagedShapes.firstIndex(where: { $0.id == id }) else {
            return nil
        }

        stagedShapes[index].center = center
        return stagedShapes[index]
    }

    func commitStagedShape(id: SceneShape.ID) -> SceneShape? {
        guard let index = stagedShapes.firstIndex(where: { $0.id == id }) else {
            return nil
        }

        let shape = stagedShapes.remove(at: index)
        shapes.append(shape)
        return shape
    }

    private func makeShape(kind: SceneShapeKind, rectangle: RectangleShape) -> SceneShape {
        let shape = SceneShape(
            id: nextShapeID,
            kind: kind,
            center: rectangle.center,
            size: rectangle.size,
            color: rectangle.color
        )
        nextShapeID += 1
        return shape
    }
}

struct SceneShape: Identifiable {
    typealias ID = UInt64

    let id: ID
    let kind: SceneShapeKind
    var center: CGPoint
    let size: CGSize
    let color: Color
}

enum SceneShapeKind {
    case rectangle
}
