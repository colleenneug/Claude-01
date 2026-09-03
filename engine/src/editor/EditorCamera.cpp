#include "forge/editor/Editor.hpp"

namespace forge {

void EditorCamera::update(float dt, const InputState& input, bool viewportHasMouse) {
    // Right mouse to look is the convention every 3D tool shares, and it
    // keeps the left button free for selection.
    const bool looking = viewportHasMouse && input.isDown(Key::MouseRight);
    if (looking) {
        rotation.yaw = Rotator::normalizeAngle(rotation.yaw - input.mouseDelta().x * lookSensitivity);
        rotation.pitch = clampf(rotation.pitch - input.mouseDelta().y * lookSensitivity, -89.0f, 89.0f);
    }

    Vec3 wish{0, 0, 0};
    if (looking) {
        // WASD only while looking, so the same keys can mean something
        // else when the pointer is over a panel.
        const Quat q = rotation.toQuat();
        if (input.isDown(Key::W)) wish += q.forward();
        if (input.isDown(Key::S)) wish -= q.forward();
        if (input.isDown(Key::D)) wish += q.right();
        if (input.isDown(Key::A)) wish -= q.right();
        if (input.isDown(Key::E)) wish += Vec3::Up;
        if (input.isDown(Key::Q)) wish -= Vec3::Up;
    }

    float speed = moveSpeed;
    if (input.isDown(Key::LeftShift)) speed *= 3.0f;
    if (input.isDown(Key::LeftControl)) speed *= 0.25f;

    if (wish.lengthSq() > 1e-6f) velocity_ = wish.normalized() * speed;
    else velocity_ = dampv(velocity_, Vec3::Zero, 14.0f, dt);
    position += velocity_ * dt;

    // Middle-drag pans in the view plane.
    if (viewportHasMouse && input.isDown(Key::MouseMiddle)) {
        const Quat q = rotation.toQuat();
        const float scale = 0.01f * std::max(1.0f, moveSpeed);
        position -= q.right() * (input.mouseDelta().x * scale);
        position += q.up() * (input.mouseDelta().y * scale);
    }

    // The wheel dollies along the view axis when looking around, and
    // otherwise adjusts how fast the camera flies — both are what the
    // wheel is expected to do, and which one depends on context.
    if (viewportHasMouse && std::fabs(input.mouseWheel()) > 0.0f) {
        if (looking) moveSpeed = clampf(moveSpeed * (1.0f + input.mouseWheel() * 0.15f), 0.5f, 200.0f);
        else position += rotation.toQuat().forward() * (input.mouseWheel() * std::max(1.0f, moveSpeed * 0.25f));
    }

    if (focusBlend_ < 1.0f) {
        focusBlend_ = std::min(1.0f, focusBlend_ + dt * 4.0f);
        // Ease so framing a selection glides rather than snapping.
        const float t = smoothStep(0.0f, 1.0f, focusBlend_);
        position = lerp(focusFrom_, focusTarget_, t);
    }
}

void EditorCamera::focusOn(const Box& bounds) {
    if (!bounds.valid()) return;
    const Vec3 centre = bounds.center();
    const float radius = std::max(0.5f, bounds.size().length() * 0.5f);
    // Back off far enough that the bounding sphere fits the vertical
    // field of view, with a margin so it does not touch the edges.
    const float distance = radius / std::tan(radians(fieldOfView * 0.5f)) * 1.6f;

    rotation = Rotator::fromDirection((centre - position).normalized());
    focusFrom_ = position;
    focusTarget_ = centre - rotation.toQuat().forward() * distance;
    focusBlend_ = 0.0f;
}

RenderView EditorCamera::view(float aspect) const {
    RenderView v;
    const Quat q = rotation.toQuat();
    v.view = Mat4::lookAt(position, position + q.forward(), q.up());
    v.projection = Mat4::perspective(fieldOfView, aspect, 0.05f, 800.0f);
    v.cameraPosition = position;
    v.nearClip = 0.05f;
    v.farClip = 800.0f;
    return v;
}

Ray EditorCamera::rayThrough(float ndcX, float ndcY, float aspect) const {
    const Quat q = rotation.toQuat();
    const float tanHalf = std::tan(radians(fieldOfView * 0.5f));
    // Reconstruct the direction directly from the field of view rather
    // than inverting the projection: fewer steps, and no near-plane edge
    // cases to get wrong.
    const Vec3 dir = q.forward() + q.right() * (ndcX * tanHalf * aspect) + q.up() * (ndcY * tanHalf);
    return Ray{position, dir.normalized()};
}

} // namespace forge
