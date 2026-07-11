-- Version: 3
-- Description: Seed system category template pool (global, read-only for users)
-- Users pick from this pool to initialize their personal categories tree.

-- =============================================================================
-- Expense board top-level categories
-- =============================================================================

INSERT INTO system_category_templates (name, group_name, parent_id, default_board, sort_order, is_selectable) VALUES
('餐饮', 'expense', NULL, 'expense', 1, TRUE),
('日常', 'expense', NULL, 'expense', 2, TRUE),
('财务', 'expense', NULL, 'expense', 3, TRUE),
('居家', 'expense', NULL, 'expense', 4, TRUE),
('服饰', 'expense', NULL, 'expense', 5, TRUE),
('美妆', 'expense', NULL, 'expense', 6, TRUE),
('数码', 'expense', NULL, 'expense', 7, TRUE),
('办公', 'expense', NULL, 'expense', 8, TRUE),
('社交', 'expense', NULL, 'expense', 9, TRUE),
('医疗', 'expense', NULL, 'expense', 10, TRUE),
('交通', 'expense', NULL, 'expense', 11, TRUE),
('运动', 'expense', NULL, 'expense', 12, TRUE),
('娱乐', 'expense', NULL, 'expense', 13, TRUE),
('教育', 'expense', NULL, 'expense', 14, TRUE),
('旅行', 'expense', NULL, 'expense', 15, TRUE),
('宠物', 'expense', NULL, 'expense', 16, TRUE),
('家庭', 'expense', NULL, 'expense', 17, TRUE),
('汽车', 'expense', NULL, 'expense', 18, TRUE),
('校园', 'expense', NULL, 'expense', 19, TRUE),
('人情', 'expense', NULL, 'expense', 20, TRUE),
('其他', 'expense', NULL, 'expense', 99, TRUE)
ON CONFLICT DO NOTHING;

-- =============================================================================
-- Income board top-level categories
-- =============================================================================

INSERT INTO system_category_templates (name, group_name, parent_id, default_board, sort_order, is_selectable) VALUES
('工资', 'income', NULL, 'income', 1, TRUE),
('奖金', 'income', NULL, 'income', 2, TRUE),
('投资', 'income', NULL, 'income', 3, TRUE),
('兼职', 'income', NULL, 'income', 4, TRUE),
('副业', 'income', NULL, 'income', 5, TRUE),
('红包', 'income', NULL, 'income', 6, TRUE)
ON CONFLICT DO NOTHING;

-- =============================================================================
-- Common second-level subcategories (餐饮)
-- =============================================================================

WITH food_parent AS (
    SELECT id FROM system_category_templates WHERE name = '餐饮' AND group_name = 'expense' AND parent_id IS NULL LIMIT 1
)
INSERT INTO system_category_templates (name, group_name, parent_id, default_board, sort_order, is_selectable)
SELECT '早餐', 'expense', id, 'expense', 1, TRUE FROM food_parent
UNION ALL
SELECT '午餐', 'expense', id, 'expense', 2, TRUE FROM food_parent
UNION ALL
SELECT '晚餐', 'expense', id, 'expense', 3, TRUE FROM food_parent
UNION ALL
SELECT '咖啡', 'expense', id, 'expense', 4, TRUE FROM food_parent
UNION ALL
SELECT '外卖', 'expense', id, 'expense', 5, TRUE FROM food_parent
UNION ALL
SELECT '聚餐', 'expense', id, 'expense', 6, TRUE FROM food_parent
ON CONFLICT DO NOTHING;

-- =============================================================================
-- Common second-level subcategories (日常)
-- =============================================================================

WITH daily_parent AS (
    SELECT id FROM system_category_templates WHERE name = '日常' AND group_name = 'expense' AND parent_id IS NULL LIMIT 1
)
INSERT INTO system_category_templates (name, group_name, parent_id, default_board, sort_order, is_selectable)
SELECT '水费', 'expense', id, 'expense', 1, TRUE FROM daily_parent
UNION ALL
SELECT '电费', 'expense', id, 'expense', 2, TRUE FROM daily_parent
UNION ALL
SELECT '燃气费', 'expense', id, 'expense', 3, TRUE FROM daily_parent
UNION ALL
SELECT '物业费', 'expense', id, 'expense', 4, TRUE FROM daily_parent
UNION ALL
SELECT '生活用品', 'expense', id, 'expense', 5, TRUE FROM daily_parent
ON CONFLICT DO NOTHING;

-- =============================================================================
-- Common second-level subcategories (交通)
-- =============================================================================

WITH transport_parent AS (
    SELECT id FROM system_category_templates WHERE name = '交通' AND group_name = 'expense' AND parent_id IS NULL LIMIT 1
)
INSERT INTO system_category_templates (name, group_name, parent_id, default_board, sort_order, is_selectable)
SELECT '地铁', 'expense', id, 'expense', 1, TRUE FROM transport_parent
UNION ALL
SELECT '公交', 'expense', id, 'expense', 2, TRUE FROM transport_parent
UNION ALL
SELECT '打车', 'expense', id, 'expense', 3, TRUE FROM transport_parent
UNION ALL
SELECT '加油', 'expense', id, 'expense', 4, TRUE FROM transport_parent
UNION ALL
SELECT '停车', 'expense', id, 'expense', 5, TRUE FROM transport_parent
ON CONFLICT DO NOTHING;

-- =============================================================================
-- Common second-level subcategories (财务)
-- =============================================================================

WITH finance_parent AS (
    SELECT id FROM system_category_templates WHERE name = '财务' AND group_name = 'expense' AND parent_id IS NULL LIMIT 1
)
INSERT INTO system_category_templates (name, group_name, parent_id, default_board, sort_order, is_selectable)
SELECT '手续费', 'expense', id, 'expense', 1, TRUE FROM finance_parent
UNION ALL
SELECT '利息支出', 'expense', id, 'expense', 2, TRUE FROM finance_parent
UNION ALL
SELECT '汇兑损耗', 'expense', id, 'expense', 3, TRUE FROM finance_parent
ON CONFLICT DO NOTHING;

-- =============================================================================
-- Common second-level subcategories (工资 - income)
-- =============================================================================

WITH salary_parent AS (
    SELECT id FROM system_category_templates WHERE name = '工资' AND group_name = 'income' AND parent_id IS NULL LIMIT 1
)
INSERT INTO system_category_templates (name, group_name, parent_id, default_board, sort_order, is_selectable)
SELECT '基本工资', 'income', id, 'income', 1, TRUE FROM salary_parent
UNION ALL
SELECT '绩效', 'income', id, 'income', 2, TRUE FROM salary_parent
UNION ALL
SELECT '补贴', 'income', id, 'income', 3, TRUE FROM salary_parent
ON CONFLICT DO NOTHING;

-- =============================================================================
-- Common second-level subcategories (投资 - income)
-- =============================================================================

WITH investment_parent AS (
    SELECT id FROM system_category_templates WHERE name = '投资' AND group_name = 'income' AND parent_id IS NULL LIMIT 1
)
INSERT INTO system_category_templates (name, group_name, parent_id, default_board, sort_order, is_selectable)
SELECT '股息', 'income', id, 'income', 1, TRUE FROM investment_parent
UNION ALL
SELECT '基金收益', 'income', id, 'income', 2, TRUE FROM investment_parent
UNION ALL
SELECT '利息', 'income', id, 'income', 3, TRUE FROM investment_parent
UNION ALL
SELECT '卖出收益', 'income', id, 'income', 4, TRUE FROM investment_parent
ON CONFLICT DO NOTHING;

-- =============================================================================
-- Common second-level subcategories (红包 - income)
-- =============================================================================

WITH redpacket_parent AS (
    SELECT id FROM system_category_templates WHERE name = '红包' AND group_name = 'income' AND parent_id IS NULL LIMIT 1
)
INSERT INTO system_category_templates (name, group_name, parent_id, default_board, sort_order, is_selectable)
SELECT '亲友红包', 'income', id, 'income', 1, TRUE FROM redpacket_parent
UNION ALL
SELECT '平台红包', 'income', id, 'income', 2, TRUE FROM redpacket_parent
ON CONFLICT DO NOTHING;
