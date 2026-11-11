import sys
p = '/root/kshimc/src/core/platform.c'
s = open(p, encoding='utf-8').read()

# Patch 1: hide unresolvable non-Android system rows on the main page
old1 = """    if (settings_system_caller(menu, target, &caller)) {
        if (menu_append(page, name,
                        kshim_menu_role_description(role,
                            menu->caller_descriptions ? menu->caller_descriptions[caller] : NULL),
                        MENU_PAYLOAD, caller, role))
            return -1;
        if (menu->default_ok && caller == menu->original_initial) {
            menu->page = MENU_PAGE_MAIN;
            page->initial = row;
        }
        *usable = 1;
    } else if (menu_append(page, name, kshim_menu_text(KSHIM_TEXT_UNAVAILABLE),
                           MENU_UNAVAILABLE, 0, role)) {
        return -1;
    }"""
new1 = """    if (settings_system_caller(menu, target, &caller)) {
        if (menu_append(page, name,
                        kshim_menu_role_description(role,
                            menu->caller_descriptions ? menu->caller_descriptions[caller] : NULL),
                        MENU_PAYLOAD, caller, role))
            return -1;
        if (menu->default_ok && caller == menu->original_initial) {
            menu->page = MENU_PAGE_MAIN;
            page->initial = row;
        }
        *usable = 1;
    } else if (target == BOOT_TARGET_ANDROID &&
               menu_append(page, name, kshim_menu_text(KSHIM_TEXT_UNAVAILABLE),
                           MENU_UNAVAILABLE, 0, role)) {
        return -1;
    }"""
assert old1 in s, 'patch1 anchor missing'
s = s.replace(old1, new1, 1)

# Patch 2: default page - same hide for unresolved non-Android targets
old2 = """            if (settings_system_caller(menu, default_targets[slot], &caller)) {
                if (menu_append(page, name,
                                kshim_menu_role_description(default_roles[slot], NULL),
                                MENU_DEFAULT_SET, default_targets[slot], default_roles[slot]))
                    return -1;
            } else if (menu_append(page, name, kshim_menu_text(KSHIM_TEXT_UNAVAILABLE),
                                   MENU_UNAVAILABLE, 0, default_roles[slot])) {
                return -1;
            }"""
new2 = """            if (settings_system_caller(menu, default_targets[slot], &caller)) {
                if (menu_append(page, name,
                                kshim_menu_role_description(default_roles[slot], NULL),
                                MENU_DEFAULT_SET, default_targets[slot], default_roles[slot]))
                    return -1;
            } else if (default_targets[slot] == BOOT_TARGET_ANDROID &&
                       menu_append(page, name, kshim_menu_text(KSHIM_TEXT_UNAVAILABLE),
                                   MENU_UNAVAILABLE, 0, default_roles[slot])) {
                return -1;
            }"""
assert old2 in s, 'patch2 anchor missing'
s = s.replace(old2, new2, 1)

# Patch 3: surface the qcom_reboot error code on the GUI status line
old3 = """        int result = kshim_qcom_reboot_request(menu->fdt, target);
        *failure = result == KSHIM_QCOM_REBOOT_ROLLBACK_FAILED ?
            KSHIM_STR_RESTORE_FAILED : KSHIM_STR_MODE_FAILED;
        printf("%s: %d\\n", *failure, result);
        return 0;"""
new3 = """        int result = kshim_qcom_reboot_request(menu->fdt, target);
        static char mode_failure[80];
        snprintf(mode_failure, sizeof(mode_failure), "%s: %d",
                 result == KSHIM_QCOM_REBOOT_ROLLBACK_FAILED ?
                     KSHIM_STR_RESTORE_FAILED : KSHIM_STR_MODE_FAILED,
                 result);
        *failure = mode_failure;
        printf("%s\\n", mode_failure);
        return 0;"""
assert old3 in s, 'patch3 anchor missing'
s = s.replace(old3, new3, 1)

open(p, 'w', encoding='utf-8').write(s)
print('all 3 patches applied')
