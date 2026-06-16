import AppKit
import Observation
import SwiftUI

@MainActor
@Observable
final class AppSettings {
    private enum Keys {
        static let sceneBackgroundColor = "scene.backgroundColor"
    }

    static let defaultSceneBackgroundColor = Color(red: 0.08, green: 0.10, blue: 0.12)

    private let userDefaults: UserDefaults
    var sceneBackgroundColor: Color {
        didSet {
            let storageValue = sceneBackgroundColor.storageValue
            guard userDefaults.array(forKey: Keys.sceneBackgroundColor) as? [Double] != storageValue else {
                return
            }
            userDefaults.set(storageValue, forKey: Keys.sceneBackgroundColor)
        }
    }

    init(userDefaults: UserDefaults = .standard) {
        self.userDefaults = userDefaults
        sceneBackgroundColor = userDefaults.color(forKey: Keys.sceneBackgroundColor)
            ?? Self.defaultSceneBackgroundColor
    }
}

private extension UserDefaults {
    func color(forKey key: String) -> Color? {
        guard let storageValue = array(forKey: key) as? [Double],
              storageValue.count == 4 else {
            return nil
        }

        return Color(
            .sRGB,
            red: storageValue[0],
            green: storageValue[1],
            blue: storageValue[2],
            opacity: storageValue[3]
        )
    }
}

private extension Color {
    var storageValue: [Double] {
        let components = rgbaComponents(in: CGColorSpace.sRGB)

        return [
            Double(components.red),
            Double(components.green),
            Double(components.blue),
            Double(components.alpha),
        ]
    }
}

extension Color {
    var linearRGBAComponents: (red: CGFloat, green: CGFloat, blue: CGFloat, alpha: CGFloat) {
        rgbaComponents(in: CGColorSpace.extendedLinearSRGB)
    }

    private func rgbaComponents(
        in colorSpaceName: CFString
    ) -> (red: CGFloat, green: CGFloat, blue: CGFloat, alpha: CGFloat) {
        let color = NSColor(self).cgColor
        let colorSpace = CGColorSpace(name: colorSpaceName)
        let convertedColor = colorSpace.flatMap {
            color.converted(to: $0, intent: CGColorRenderingIntent.defaultIntent, options: nil)
        }
        let components = convertedColor?.components ?? color.components ?? [0.0, 0.0, 0.0, 1.0]

        func component(_ index: Int, fallback: CGFloat) -> CGFloat {
            components.indices.contains(index) ? components[index] : fallback
        }

        return (
            red: component(0, fallback: 0.0),
            green: component(1, fallback: 0.0),
            blue: component(2, fallback: 0.0),
            alpha: component(3, fallback: 1.0)
        )
    }
}

extension VisualRuntimeSession.SceneSettings {
    init(backgroundColor: Color) {
        let color = backgroundColor.linearRGBAComponents
        self.init(
            backgroundColor: VisualRuntimeSession.ColorRGBA(
                red: Float(color.red),
                green: Float(color.green),
                blue: Float(color.blue),
                alpha: Float(color.alpha)
            )
        )
    }
}
