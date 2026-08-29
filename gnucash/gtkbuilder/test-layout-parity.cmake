# Verify every declarative GTK4 resource and the GTK3-to-GTK4 layout invariants
# that cannot be represented by a GtkBox default alone.
if (NOT DEFINED GTK4_BUILDER_TOOL OR NOT DEFINED GTKBUILDER_DIR OR NOT DEFINED GNC_SOURCE_DIR OR NOT DEFINED AQB_ASSISTANT)
    message(FATAL_ERROR "GTK builder layout parity test was invoked without its source paths")
endif()

file(GLOB_RECURSE builder_files "${GNC_SOURCE_DIR}/*.glade" "${GNC_SOURCE_DIR}/*.ui")
list(LENGTH builder_files builder_file_count)
if (NOT builder_file_count EQUAL 91)
    message(FATAL_ERROR "Expected 91 tracked declarative GTK resources, found ${builder_file_count}")
endif()
foreach (builder_file IN LISTS builder_files)
    execute_process(
        COMMAND "${GTK4_BUILDER_TOOL}" validate "${builder_file}"
        RESULT_VARIABLE validate_result
        OUTPUT_VARIABLE validate_output
        ERROR_VARIABLE validate_error)
    if (NOT validate_result EQUAL 0)
        message(FATAL_ERROR "GTK4 Builder validation failed for ${builder_file}: ${validate_output}${validate_error}")
    endif()
    execute_process(
        COMMAND "${GTK4_BUILDER_TOOL}" enumerate "${builder_file}"
        RESULT_VARIABLE enumerate_result
        OUTPUT_VARIABLE enumerate_output
        ERROR_VARIABLE enumerate_error)
    if (NOT enumerate_result EQUAL 0)
        message(FATAL_ERROR "GTK4 Builder enumeration failed for ${builder_file}: ${enumerate_output}${enumerate_error}")
    endif()
endforeach()

function (read_box_properties relative_file object_id out_var)
    file(READ "${GTKBUILDER_DIR}/${relative_file}" contents)
    string(FIND "${contents}" "<object class=\"GtkBox\" id=\"${object_id}\">" object_offset)
    if (object_offset EQUAL -1)
        message(FATAL_ERROR "Missing GtkBox ${object_id} in ${relative_file}")
    endif()
    string(SUBSTRING "${contents}" ${object_offset} 2048 object_prefix)
    string(FIND "${object_prefix}" "<child" first_child)
    if (first_child EQUAL -1)
        message(FATAL_ERROR "GtkBox ${object_id} in ${relative_file} has no child boundary")
    endif()
    string(SUBSTRING "${object_prefix}" 0 ${first_child} properties)
    set(${out_var} "${properties}" PARENT_SCOPE)
endfunction()

function (require_box_property relative_file object_id name value)
    read_box_properties("${relative_file}" "${object_id}" properties)
    string(FIND "${properties}" "<property name=\"${name}\">${value}</property>" property_offset)
    if (property_offset EQUAL -1)
        message(FATAL_ERROR "GtkBox ${object_id} in ${relative_file} must retain ${name}=${value}")
    endif()
endfunction()

function (require_widget_property relative_file object_id name value_pattern)
    file(READ "${GTKBUILDER_DIR}/${relative_file}" contents)
    string(REGEX MATCH "<object class=\"[^\"]+\" id=\"${object_id}\">[ \t\r\n]*(<property name=\"[^\"]+\"[^>]*>[^<]*</property>[ \t\r\n]*)*" widget_prefix "${contents}")
    string(REGEX MATCH "<property name=\"${name}\"[^>]*>(${value_pattern})</property>" property_match "${widget_prefix}")
    if (NOT property_match)
        message(FATAL_ERROR "Widget ${object_id} in ${relative_file} must retain ${name} matching ${value_pattern}")
    endif()
endfunction()

function (require_layout_property relative_file object_id name value)
    file(READ "${GTKBUILDER_DIR}/${relative_file}" contents)
    string(FIND "${contents}" "id=\"${object_id}\"" object_offset)
    if (object_offset EQUAL -1)
        message(FATAL_ERROR "Missing widget ${object_id} in ${relative_file}")
    endif()
    string(SUBSTRING "${contents}" ${object_offset} 4096 object_suffix)
    string(FIND "${object_suffix}" "<layout>" layout_offset)
    if (layout_offset EQUAL -1)
        message(FATAL_ERROR "Widget ${object_id} in ${relative_file} has no GtkGrid layout")
    endif()
    string(SUBSTRING "${object_suffix}" ${layout_offset} 1024 layout_prefix)
    string(FIND "${layout_prefix}" "<property name=\"${name}\">${value}</property>" property_offset)
    if (property_offset EQUAL -1)
        message(FATAL_ERROR "Widget ${object_id} in ${relative_file} must retain grid ${name}=${value}")
    endif()
endfunction()

function (require_window_property relative_file window_id name value)
    file(READ "${GTKBUILDER_DIR}/${relative_file}" contents)
    string(FIND "${contents}" "<object class=\"GtkWindow\" id=\"${window_id}\">" object_offset)
    if (object_offset EQUAL -1)
        message(FATAL_ERROR "Missing GtkWindow ${window_id} in ${relative_file}")
    endif()
    string(SUBSTRING "${contents}" ${object_offset} 2048 object_prefix)
    string(FIND "${object_prefix}" "<child" first_child)
    string(SUBSTRING "${object_prefix}" 0 ${first_child} properties)
    string(FIND "${properties}" "<property name=\"${name}\">${value}</property>" property_offset)
    if (property_offset EQUAL -1)
        message(FATAL_ERROR "GtkWindow ${window_id} in ${relative_file} must retain ${name}=${value}")
    endif()
endfunction()

# GtkButtonBox layout-style=end became GtkBox. These action areas must stay
# grouped at the end with the GTK3 standard six-pixel inter-button gap.
set(end_areas
    "assistant-qif-import.glade|load_progress_hbuttonbox1"
    "assistant-qif-import.glade|convert_progress_hbuttonbox1"
    "dialog-account.glade|dialog-action_area100"
    "dialog-account.glade|dialog-action_area200"
    "dialog-account.glade|dialog-action_area400"
    "dialog-billterms.glade|dialog-action_area"
    "dialog-choose-owner.glade|dialog-action_area3"
    "dialog-commodity.ui|dialog-action_area4"
    "dialog-doclink.ui|buttonbox"
    "dialog-find-account.glade|buttonbox"
    "dialog-imap-editor.ui|dialog-action_area1"
    "dialog-import.glade|dialog-action_area14"
    "dialog-new-user.glade|hbbox1"
    "dialog-price.ui|dialog-action_area2"
    "dialog-progress.glade|hbuttonbox1"
    "dialog-report.glade|dialog-action_area"
    "dialog-sx.ui|dialog-action_area24"
    "dialog-sx.ui|dialog-action_area23"
    "dialog-transfer.glade|hbbox"
    "gnc-plugin-page-budget.ui|dialog-action_area6"
    "gnc-plugin-page-budget.ui|dialog-action_area5"
    "gnc-tree-view-owner.glade|dialog-action_area13")
foreach (entry IN LISTS end_areas)
    string(REPLACE "|" ";" fields "${entry}")
    list(GET fields 0 relative_file)
    list(GET fields 1 object_id)
    require_box_property("${relative_file}" "${object_id}" "halign" "end")
    require_box_property("${relative_file}" "${object_id}" "spacing" "6")
endforeach()

# GTK3 ButtonBox secondary children must remain on the leading edge. The
# enclosing GtkBox expands, while only the original secondary child consumes
# that space; the primary action group therefore remains right-aligned.
set(secondary_areas
    "dialog-account.glade|dialog-action_area300|help_button"
    "dialog-bi-import-gui.glade|dialog-action_area3|helpbutton"
    "dialog-book-close.ui|dialog-action_area1|helpbutton"
    "dialog-commodity.ui|dialog-action_area6|help_button"
    "dialog-customer.glade|dialog-action_area1|helpbutton"
    "dialog-fincalc.ui|dialog-action_area10|help_button"
    "dialog-import.glade|dialog-action_area10|matcher__help"
    "dialog-invoice.glade|dialog-action_area2|helpbutton"
    "dialog-options.glade|buttonbox|helpbutton"
    "dialog-price.ui|dialog-action_area18|pd_help_button"
    "dialog-print-check.glade|dialog-action_area6|helpbutton"
    "dialog-search.glade|dialog-action_area3|help_button"
    "dialog-sx.ui|dialog-action_area17|help_button"
    "dialog-sx.ui|dialog-action_area25|helpbutton2"
    "dialog-vendor.glade|dialog-action_area1|helpbutton"
    "dialog-custom-report.glade|custom_report_actions|help_button"
    "dialog-customer-import-gui.glade|customer_import_actions|helpbutton"
    "dialog-employee.glade|employee_actions|helpbutton"
    "dialog-job.glade|job_actions|helpbutton"
    "dialog-order.glade|order_actions|helpbutton"
    "dialog-order.glade|new_order_actions|help_button"
    "dialog-preferences.glade|preferences_action_area|helpbutton2")
foreach (entry IN LISTS secondary_areas)
    string(REPLACE "|" ";" fields "${entry}")
    list(GET fields 0 relative_file)
    list(GET fields 1 box_id)
    list(GET fields 2 child_id)
    require_box_property("${relative_file}" "${box_id}" "hexpand" "1")
    require_box_property("${relative_file}" "${box_id}" "spacing" "6")
    require_widget_property("${relative_file}" "${child_id}" "hexpand" "1|true")
    require_widget_property("${relative_file}" "${child_id}" "halign" "start")
endforeach()

# These were the only non-default alignment deltas in the GTK3/GTK4 matrix.
# Keeping their exact values prevents a builder regeneration from silently
# reintroducing the visually observable layout shifts.
set(alignment_invariants
    "assistant-csv-trans-import.glade|start_row|halign|start"
    "assistant-csv-trans-import.glade|end_row|halign|start"
    "assistant-loan.glade|pay_back_button|halign|start"
    "assistant-loan.glade|pay_next_button|halign|start"
    "dialog-commodities.ui|rename_namespace_button|halign|start"
    "dialog-commodity.ui|label824|halign|start"
    "dialog-date-close.glade|hbox2|valign|start"
    "dialog-employee.glade|label34|halign|end"
    "dialog-invoice.glade|hbox1|valign|start"
    "dialog-invoice.glade|page_proj_frame|valign|start"
    "dialog-invoice.glade|to_charge_frame|valign|start"
    "dialog-invoice.glade|yes_tt_reset|halign|start"
    "dialog-invoice.glade|no_tt_reset|halign|start"
    "dialog-new-user.glade|table1|valign|start"
    "dialog-sx.ui|end_on_date_button|halign|start"
    "dialog-sx.ui|n_occurrences_button|halign|start"
    "dialog-sx.ui|label847810|halign|start"
    "dialog-sx.ui|label847808|halign|start"
    "dialog-sx.ui|review_txn_toggle|halign|start"
    "gnc-frequency.ui|label847759|halign|start"
    "gnc-frequency.ui|semimonthly_first|halign|start"
    "gnc-frequency.ui|label847751|halign|start"
    "gnc-frequency.ui|label847760|halign|start"
    "gnc-frequency.ui|semimonthly_second|halign|start"
    "gnc-frequency.ui|label847752|halign|start"
    "gnc-frequency.ui|label847756|halign|start"
    "gnc-frequency.ui|monthly_day|halign|start"
    "gnc-frequency.ui|label847750|halign|start"
    "gnc-frequency.ui|monthly_weekend|halign|start"
    "gnc-plugin-page-register.glade|sort_save|halign|start")
foreach (entry IN LISTS alignment_invariants)
    string(REPLACE "|" ";" fields "${entry}")
    list(GET fields 0 relative_file)
    list(GET fields 1 object_id)
    list(GET fields 2 property_name)
    list(GET fields 3 expected_value)
    require_widget_property("${relative_file}" "${object_id}" "${property_name}" "${expected_value}")
endforeach()

set(grid_invariants
    "dialog-price.ui|date_hbox|row|2"
    "dialog-price.ui|remove_namespace_label|row|3"
    "dialog-price.ui|namespace_combo_we|row|3"
    "dialog-price.ui|label3|row|4"
    "dialog-price.ui|hbox3|row|5"
    "dialog-price.ui|label2|row|6"
    "dialog-price.ui|hbox2|row|7"
    "dialog-tax-info.glade|identity_edit_button|column|0"
    "dialog-tax-info.glade|identity_edit_button|row|2"
    "dialog-tax-info.glade|identity_edit_button|column-span|2")
foreach (entry IN LISTS grid_invariants)
    string(REPLACE "|" ";" fields "${entry}")
    list(GET fields 0 relative_file)
    list(GET fields 1 object_id)
    list(GET fields 2 property_name)
    list(GET fields 3 expected_value)
    require_layout_property("${relative_file}" "${object_id}" "${property_name}" "${expected_value}")
endforeach()

require_window_property("assistant-hierarchy.glade" "hierarchy_assistant" "default-width" "400")
require_window_property("assistant-hierarchy.glade" "hierarchy_assistant" "default-height" "550")
require_window_property("dialog-lot-viewer.glade" "lot_viewer_dialog" "default-width" "600")
require_window_property("dialog-lot-viewer.glade" "lot_viewer_dialog" "default-height" "400")

file(READ "${AQB_ASSISTANT}" aqb_assistant_contents)
string(FIND "${aqb_assistant_contents}" "<property name=\"default-width\">500</property>" aqb_width_offset)
string(FIND "${aqb_assistant_contents}" "<property name=\"default-height\">500</property>" aqb_height_offset)
if (aqb_width_offset EQUAL -1 OR aqb_height_offset EQUAL -1)
    message(FATAL_ERROR "The AqBanking initial assistant must retain its GTK3-effective 500x500 default size")
endif()
