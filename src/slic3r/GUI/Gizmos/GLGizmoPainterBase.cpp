// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoPainterBase.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosCommon.hpp"

#include <GL/glew.h>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Model.hpp"



namespace Slic3r {
namespace GUI {


GLGizmoPainterBase::GLGizmoPainterBase(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
{
    m_clipping_plane.reset(new ClippingPlane());
}



void GLGizmoPainterBase::activate_internal_undo_redo_stack(bool activate)
{
    if (activate && ! m_internal_stack_active) {
        wxString str = get_painter_type() == PainterGizmoType::FDM_SUPPORTS
                           ? _L("Supports gizmo turned on")
                           : _L("Seam gizmo turned on");
        Plater::TakeSnapshot(wxGetApp().plater(), str);
        wxGetApp().plater()->enter_gizmos_stack();
        m_internal_stack_active = true;
    }
    if (! activate && m_internal_stack_active) {
        wxString str = get_painter_type() == PainterGizmoType::SEAM
                           ? _L("Seam gizmo turned off")
                           : _L("Supports gizmo turned off");
        wxGetApp().plater()->leave_gizmos_stack();
        Plater::TakeSnapshot(wxGetApp().plater(), str);
        m_internal_stack_active = false;
    }
}



void GLGizmoPainterBase::set_painter_gizmo_data(const Selection& selection)
{
    if (m_state != On)
        return;

    const ModelObject* mo = m_c->selection_info() ? m_c->selection_info()->model_object() : nullptr;

    if (mo && selection.is_from_single_instance()
     && (m_schedule_update || mo->id() != m_old_mo_id || mo->volumes.size() != m_old_volumes_size))
    {
        update_from_model_object();
        m_old_mo_id = mo->id();
        m_old_volumes_size = mo->volumes.size();
        m_schedule_update = false;
    }
}



void GLGizmoPainterBase::render_triangles(const Selection& selection) const
{
    const ModelObject* mo = m_c->selection_info()->model_object();

    glsafe(::glEnable(GL_POLYGON_OFFSET_FILL));
    ScopeGuard offset_fill_guard([]() { glsafe(::glDisable(GL_POLYGON_OFFSET_FILL)); } );
    glsafe(::glPolygonOffset(-1.0, 1.0));

    // Take care of the clipping plane. The normal of the clipping plane is
    // saved with opposite sign than we need to pass to OpenGL (FIXME)
    bool clipping_plane_active = m_c->object_clipper()->get_position() != 0.;
    if (clipping_plane_active) {
        const ClippingPlane* clp = m_c->object_clipper()->get_clipping_plane();
        double clp_data[4];
        memcpy(clp_data, clp->get_data(), 4 * sizeof(double));
        for (int i=0; i<3; ++i)
            clp_data[i] = -1. * clp_data[i];

        glsafe(::glClipPlane(GL_CLIP_PLANE0, (GLdouble*)clp_data));
        glsafe(::glEnable(GL_CLIP_PLANE0));
    }

    int mesh_id = -1;
    for (const ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;

        ++mesh_id;

        const Transform3d trafo_matrix =
            mo->instances[selection.get_instance_idx()]->get_transformation().get_matrix() *
            mv->get_matrix();

        bool is_left_handed = trafo_matrix.matrix().determinant() < 0.;
        if (is_left_handed)
            glsafe(::glFrontFace(GL_CW));

        glsafe(::glPushMatrix());
        glsafe(::glMultMatrixd(trafo_matrix.data()));

        m_triangle_selectors[mesh_id]->render(m_imgui);

        glsafe(::glPopMatrix());
        if (is_left_handed)
            glsafe(::glFrontFace(GL_CCW));
    }
    if (clipping_plane_active)
        glsafe(::glDisable(GL_CLIP_PLANE0));
}


void GLGizmoPainterBase::render_cursor_circle() const
{
    const Camera& camera = wxGetApp().plater()->get_camera();
    float zoom = (float)camera.get_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;

    Size cnv_size = m_parent.get_canvas_size();
    float cnv_half_width = 0.5f * (float)cnv_size.get_width();
    float cnv_half_height = 0.5f * (float)cnv_size.get_height();
    if ((cnv_half_width == 0.0f) || (cnv_half_height == 0.0f))
        return;
    Vec2d mouse_pos(m_parent.get_local_mouse_position()(0), m_parent.get_local_mouse_position()(1));
    Vec2d center(mouse_pos(0) - cnv_half_width, cnv_half_height - mouse_pos(1));
    center = center * inv_zoom;

    glsafe(::glLineWidth(1.5f));
    float color[3];
    color[0] = 0.f;
    color[1] = 1.f;
    color[2] = 0.3f;
    glsafe(::glColor3fv(color));
    glsafe(::glDisable(GL_DEPTH_TEST));

    glsafe(::glPushMatrix());
    glsafe(::glLoadIdentity());
    // ensure that the circle is renderered inside the frustrum
    glsafe(::glTranslated(0.0, 0.0, -(camera.get_near_z() + 0.5)));
    // ensure that the overlay fits the frustrum near z plane
    double gui_scale = camera.get_gui_scale();
    glsafe(::glScaled(gui_scale, gui_scale, 1.0));

    glsafe(::glPushAttrib(GL_ENABLE_BIT));
    glsafe(::glLineStipple(4, 0xAAAA));
    glsafe(::glEnable(GL_LINE_STIPPLE));

    ::glBegin(GL_LINE_LOOP);
    for (double angle=0; angle<2*M_PI; angle+=M_PI/20.)
        ::glVertex2f(GLfloat(center.x()+m_cursor_radius*cos(angle)), GLfloat(center.y()+m_cursor_radius*sin(angle)));
    glsafe(::glEnd());

    glsafe(::glPopAttrib());
    glsafe(::glPopMatrix());
}



bool GLGizmoPainterBase::is_mesh_point_clipped(const Vec3d& point) const
{
    if (m_c->object_clipper()->get_position() == 0.)
        return false;

    auto sel_info = m_c->selection_info();
    int active_inst = m_c->selection_info()->get_active_instance();
    const ModelInstance* mi = sel_info->model_object()->instances[active_inst];
    const Transform3d& trafo = mi->get_transformation().get_matrix();

    Vec3d transformed_point =  trafo * point;
    transformed_point(2) += sel_info->get_sla_shift();
    return m_c->object_clipper()->get_clipping_plane()->is_point_clipped(transformed_point);
}


// Following function is called from GLCanvas3D to inform the gizmo about a mouse/keyboard event.
// The gizmo has an opportunity to react - if it does, it should return true so that the Canvas3D is
// aware that the event was reacted to and stops trying to make different sense of it. If the gizmo
// concludes that the event was not intended for it, it should return false.
bool GLGizmoPainterBase::gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down)
{
    if (action == SLAGizmoEventType::MouseWheelUp
     || action == SLAGizmoEventType::MouseWheelDown) {
        if (control_down) {
            double pos = m_c->object_clipper()->get_position();
            pos = action == SLAGizmoEventType::MouseWheelDown
                      ? std::max(0., pos - 0.01)
                      : std::min(1., pos + 0.01);
            m_c->object_clipper()->set_position(pos, true);
            return true;
        }
        else if (alt_down) {
            m_cursor_radius = action == SLAGizmoEventType::MouseWheelDown
                    ? std::max(m_cursor_radius - CursorRadiusStep, CursorRadiusMin)
                    : std::min(m_cursor_radius + CursorRadiusStep, CursorRadiusMax);
            m_parent.set_as_dirty();
            return true;
        }
    }

    if (action == SLAGizmoEventType::ResetClippingPlane) {
        m_c->object_clipper()->set_position(-1., false);
        return true;
    }

    if (action == SLAGizmoEventType::LeftDown
     || action == SLAGizmoEventType::RightDown
    || (action == SLAGizmoEventType::Dragging && m_button_down != Button::None)) {

        if (m_triangle_selectors.empty())
            return false;

        EnforcerBlockerType new_state = EnforcerBlockerType::NONE;
        if (! shift_down) {
            if (action == SLAGizmoEventType::Dragging)
                new_state = m_button_down == Button::Left
                        ? EnforcerBlockerType::ENFORCER
                        : EnforcerBlockerType::BLOCKER;
            else
                new_state = action == SLAGizmoEventType::LeftDown
                        ? EnforcerBlockerType::ENFORCER
                        : EnforcerBlockerType::BLOCKER;
        }

        const Camera& camera = wxGetApp().plater()->get_camera();
        const Selection& selection = m_parent.get_selection();
        const ModelObject* mo = m_c->selection_info()->model_object();
        const ModelInstance* mi = mo->instances[selection.get_instance_idx()];
        const Transform3d& instance_trafo = mi->get_transformation().get_matrix();

        // List of mouse positions that will be used as seeds for painting.
        std::vector<Vec2d> mouse_positions{mouse_position};

        // In case current mouse position is far from the last one,
        // add several positions from between into the list, so there
        // are no gaps in the painted region.
        {
            if (m_last_mouse_position == Vec2d::Zero())
                m_last_mouse_position = mouse_position;
            // resolution describes minimal distance limit using circle radius
            // as a unit (e.g., 2 would mean the patches will be touching).
            double resolution = 0.7;
            double diameter_px =  resolution  * m_cursor_radius * camera.get_zoom();
            int patches_in_between = int(((mouse_position - m_last_mouse_position).norm() - diameter_px) / diameter_px);
            if (patches_in_between > 0) {
                Vec2d diff = (mouse_position - m_last_mouse_position)/(patches_in_between+1);
                for (int i=1; i<=patches_in_between; ++i)
                    mouse_positions.emplace_back(m_last_mouse_position + i*diff);
            }
        }
        m_last_mouse_position = Vec2d::Zero(); // only actual hits should be saved

        // Now "click" into all the prepared points and spill paint around them.
        for (const Vec2d& mp : mouse_positions) {
            std::vector<std::vector<std::pair<Vec3f, size_t>>> hit_positions_and_facet_ids;
            bool clipped_mesh_was_hit = false;

            Vec3f normal =  Vec3f::Zero();
            Vec3f hit = Vec3f::Zero();
            size_t facet = 0;
            Vec3f closest_hit = Vec3f::Zero();
            double closest_hit_squared_distance = std::numeric_limits<double>::max();
            size_t closest_facet = 0;
            int closest_hit_mesh_id = -1;

            // Transformations of individual meshes
            std::vector<Transform3d> trafo_matrices;

            int mesh_id = -1;
            // Cast a ray on all meshes, pick the closest hit and save it for the respective mesh
            for (const ModelVolume* mv : mo->volumes) {
                if (! mv->is_model_part())
                    continue;

                ++mesh_id;

                trafo_matrices.push_back(instance_trafo * mv->get_matrix());
                hit_positions_and_facet_ids.push_back(std::vector<std::pair<Vec3f, size_t>>());

                if (m_c->raycaster()->raycasters()[mesh_id]->unproject_on_mesh(
                           mp,
                           trafo_matrices[mesh_id],
                           camera,
                           hit,
                           normal,
                           m_clipping_plane.get(),
                           &facet))
                {
                    // In case this hit is clipped, skip it.
                    if (is_mesh_point_clipped(hit.cast<double>())) {
                        clipped_mesh_was_hit = true;
                        continue;
                    }

                    // Is this hit the closest to the camera so far?
                    double hit_squared_distance = (camera.get_position()-trafo_matrices[mesh_id]*hit.cast<double>()).squaredNorm();
                    if (hit_squared_distance < closest_hit_squared_distance) {
                        closest_hit_squared_distance = hit_squared_distance;
                        closest_facet = facet;
                        closest_hit_mesh_id = mesh_id;
                        closest_hit = hit;
                    }
                }
            }

            bool dragging_while_painting = (action == SLAGizmoEventType::Dragging && m_button_down != Button::None);

            // The mouse button click detection is enabled when there is a valid hit
            // or when the user clicks the clipping plane. Missing the object entirely
            // shall not capture the mouse.
            if (closest_hit_mesh_id != -1 || clipped_mesh_was_hit) {
                if (m_button_down == Button::None)
                    m_button_down = ((action == SLAGizmoEventType::LeftDown) ? Button::Left : Button::Right);
            }

            if (closest_hit_mesh_id == -1) {
                // In case we have no valid hit, we can return. The event will
                // be stopped in following two cases:
                //  1. clicking the clipping plane
                //  2. dragging while painting (to prevent scene rotations and moving the object)
                return clipped_mesh_was_hit
                    || dragging_while_painting;
            }

            // Find respective mesh id.
            mesh_id = -1;
            for (const ModelVolume* mv : mo->volumes) {
                if (! mv->is_model_part())
                    continue;
                ++mesh_id;
                if (mesh_id == closest_hit_mesh_id)
                    break;
            }

            const Transform3d& trafo_matrix = trafo_matrices[mesh_id];

            // Calculate how far can a point be from the line (in mesh coords).
            // FIXME: The scaling of the mesh can be non-uniform.
            const Vec3d sf = Geometry::Transformation(trafo_matrix).get_scaling_factor();
            const float avg_scaling = (sf(0) + sf(1) + sf(2))/3.;
            const float limit = m_cursor_radius/avg_scaling;

            // Calculate direction from camera to the hit (in mesh coords):
            Vec3f camera_pos = (trafo_matrix.inverse() * camera.get_position()).cast<float>();
            Vec3f dir = (closest_hit - camera_pos).normalized();

            assert(mesh_id < int(m_triangle_selectors.size()));
            m_triangle_selectors[mesh_id]->select_patch(closest_hit, closest_facet, camera_pos,
                                              dir, limit, new_state);
            m_last_mouse_position = mouse_position;
        }

        return true;
    }

    if ((action == SLAGizmoEventType::LeftUp || action == SLAGizmoEventType::RightUp)
      && m_button_down != Button::None) {
        // Take snapshot and update ModelVolume data.
        wxString action_name;
        if (get_painter_type() == PainterGizmoType::FDM_SUPPORTS) {
            if (shift_down)
                action_name = _L("Remove selection");
            else {
                if (m_button_down == Button::Left)
                    action_name = _L("Add supports");
                else
                    action_name = _L("Block supports");
            }
        }
        if (get_painter_type() == PainterGizmoType::SEAM) {
            if (shift_down)
                action_name = _L("Remove selection");
            else {
                if (m_button_down == Button::Left)
                    action_name = _L("Enforce seam");
                else
                    action_name = _L("Block seam");
            }
        }

        activate_internal_undo_redo_stack(true);
        Plater::TakeSnapshot(wxGetApp().plater(), action_name);
        update_model_object();

        m_button_down = Button::None;
        m_last_mouse_position = Vec2d::Zero();
        return true;
    }

    return false;
}



bool GLGizmoPainterBase::on_is_activable() const
{
    const Selection& selection = m_parent.get_selection();

    if (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() != ptFFF
        || !selection.is_single_full_instance())
        return false;

    // Check that none of the selected volumes is outside. Only SLA auxiliaries (supports) are allowed outside.
    const Selection::IndicesList& list = selection.get_volume_idxs();
    for (const auto& idx : list)
        if (selection.get_volume(idx)->is_outside)
            return false;

    return true;
}

bool GLGizmoPainterBase::on_is_selectable() const
{
    return (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptFFF
         && wxGetApp().get_mode() != comSimple );
}


CommonGizmosDataID GLGizmoPainterBase::on_get_requirements() const
{
    return CommonGizmosDataID(
                int(CommonGizmosDataID::SelectionInfo)
              | int(CommonGizmosDataID::InstancesHider)
              | int(CommonGizmosDataID::Raycaster)
              | int(CommonGizmosDataID::ObjectClipper));
}


void GLGizmoPainterBase::on_set_state()
{
    if (m_state == m_old_state)
        return;

    if (m_state == On && m_old_state != On) { // the gizmo was just turned on
        on_opening();
        if (! m_parent.get_gizmos_manager().is_serializing()) {
            wxGetApp().CallAfter([this]() {
                activate_internal_undo_redo_stack(true);
            });
        }
    }
    if (m_state == Off && m_old_state != Off) { // the gizmo was just turned Off
        // we are actually shutting down
        on_shutdown();
        activate_internal_undo_redo_stack(false);
        m_old_mo_id = -1;
        //m_iva.release_geometry();
        m_triangle_selectors.clear();
    }
    m_old_state = m_state;
}



void GLGizmoPainterBase::on_load(cereal::BinaryInputArchive&)
{
    // We should update the gizmo from current ModelObject, but it is not
    // possible at this point. That would require having updated selection and
    // common gizmos data, which is not done at this point. Instead, save
    // a flag to do the update in set_painter_gizmo_data, which will be called
    // soon after.
    m_schedule_update = true;
}



void TriangleSelectorGUI::render(ImGuiWrapper* imgui)
{
    int enf_cnt = 0;
    int blc_cnt = 0;

    m_iva_enforcers.release_geometry();
    m_iva_blockers.release_geometry();

    for (const Triangle& tr : m_triangles) {
        if (! tr.valid || tr.is_split() || tr.get_state() == EnforcerBlockerType::NONE)
            continue;

        GLIndexedVertexArray& va = tr.get_state() == EnforcerBlockerType::ENFORCER
                                   ? m_iva_enforcers
                                   : m_iva_blockers;
        int& cnt = tr.get_state() == EnforcerBlockerType::ENFORCER
                ? enf_cnt
                : blc_cnt;

        for (int i=0; i<3; ++i)
            va.push_geometry(double(m_vertices[tr.verts_idxs[i]].v[0]),
                             double(m_vertices[tr.verts_idxs[i]].v[1]),
                             double(m_vertices[tr.verts_idxs[i]].v[2]),
                             0., 0., 1.);
        va.push_triangle(cnt,
                         cnt+1,
                         cnt+2);
        cnt += 3;
    }

    m_iva_enforcers.finalize_geometry(true);
    m_iva_blockers.finalize_geometry(true);

    if (m_iva_enforcers.has_VBOs()) {
        ::glColor4f(0.f, 0.f, 1.f, 0.2f);
        m_iva_enforcers.render();
    }


    if (m_iva_blockers.has_VBOs()) {
        ::glColor4f(1.f, 0.f, 0.f, 0.2f);
        m_iva_blockers.render();
    }


#ifdef PRUSASLICER_TRIANGLE_SELECTOR_DEBUG
    if (imgui)
        render_debug(imgui);
    else
        assert(false); // If you want debug output, pass ptr to ImGuiWrapper.
#endif
}



#ifdef PRUSASLICER_TRIANGLE_SELECTOR_DEBUG
void TriangleSelectorGUI::render_debug(ImGuiWrapper* imgui)
{
    imgui->begin(std::string("TriangleSelector dialog (DEV ONLY)"),
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
    static float edge_limit = 1.f;
    imgui->text("Edge limit (mm): ");
    imgui->slider_float("", &edge_limit, 0.1f, 8.f);
    set_edge_limit(edge_limit);
    imgui->checkbox("Show split triangles: ", m_show_triangles);
    imgui->checkbox("Show invalid triangles: ", m_show_invalid);

    int valid_triangles = m_triangles.size() - m_invalid_triangles;
    imgui->text("Valid triangles: " + std::to_string(valid_triangles) +
                  "/" + std::to_string(m_triangles.size()));
    imgui->text("Vertices: " + std::to_string(m_vertices.size()));
    if (imgui->button("Force garbage collection"))
        garbage_collect();

    if (imgui->button("Serialize - deserialize")) {
        auto map = serialize();
        deserialize(map);
    }

    imgui->end();

    if (! m_show_triangles)
        return;

    enum vtype {
        ORIGINAL = 0,
        SPLIT,
        INVALID
    };

    for (auto& va : m_varrays)
        va.release_geometry();

    std::array<int, 3> cnts;

    ::glScalef(1.01f, 1.01f, 1.01f);

    for (int tr_id=0; tr_id<int(m_triangles.size()); ++tr_id) {
        const Triangle& tr = m_triangles[tr_id];
        GLIndexedVertexArray* va = nullptr;
        int* cnt = nullptr;
        if (tr_id < m_orig_size_indices) {
            va = &m_varrays[ORIGINAL];
            cnt = &cnts[ORIGINAL];
        }
        else if (tr.valid) {
            va = &m_varrays[SPLIT];
            cnt = &cnts[SPLIT];
        }
        else {
            if (! m_show_invalid)
                continue;
            va = &m_varrays[INVALID];
            cnt = &cnts[INVALID];
        }

        for (int i=0; i<3; ++i)
            va->push_geometry(double(m_vertices[tr.verts_idxs[i]].v[0]),
                              double(m_vertices[tr.verts_idxs[i]].v[1]),
                              double(m_vertices[tr.verts_idxs[i]].v[2]),
                              0., 0., 1.);
        va->push_triangle(*cnt,
                          *cnt+1,
                          *cnt+2);
        *cnt += 3;
    }

    ::glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
    for (vtype i : {ORIGINAL, SPLIT, INVALID}) {
        GLIndexedVertexArray& va = m_varrays[i];
        va.finalize_geometry(true);
        if (va.has_VBOs()) {
            switch (i) {
            case ORIGINAL : ::glColor3f(0.f, 0.f, 1.f); break;
            case SPLIT    : ::glColor3f(1.f, 0.f, 0.f); break;
            case INVALID  : ::glColor3f(1.f, 1.f, 0.f); break;
            }
            va.render();
        }
    }
    ::glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
}
#endif



} // namespace GUI
} // namespace Slic3r
