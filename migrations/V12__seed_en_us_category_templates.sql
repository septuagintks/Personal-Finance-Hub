-- Version: 12
-- Description: Seed the complete en-US system category template tree
--
-- This mirrors the zh-CN structure introduced by V3. Locale templates are
-- initialization data only; changing a user's locale never renames existing
-- personal categories.

INSERT INTO system_category_templates (
    name, locale, group_name, parent_id, default_board, sort_order, is_selectable)
VALUES
    ('Food', 'en-US', 'expense', NULL, 'expense'::category_board, 1, TRUE),
    ('Daily Living', 'en-US', 'expense', NULL, 'expense'::category_board, 2, TRUE),
    ('Finance', 'en-US', 'expense', NULL, 'expense'::category_board, 3, TRUE),
    ('Home', 'en-US', 'expense', NULL, 'expense'::category_board, 4, TRUE),
    ('Clothing', 'en-US', 'expense', NULL, 'expense'::category_board, 5, TRUE),
    ('Beauty', 'en-US', 'expense', NULL, 'expense'::category_board, 6, TRUE),
    ('Electronics', 'en-US', 'expense', NULL, 'expense'::category_board, 7, TRUE),
    ('Office', 'en-US', 'expense', NULL, 'expense'::category_board, 8, TRUE),
    ('Social', 'en-US', 'expense', NULL, 'expense'::category_board, 9, TRUE),
    ('Healthcare', 'en-US', 'expense', NULL, 'expense'::category_board, 10, TRUE),
    ('Transportation', 'en-US', 'expense', NULL, 'expense'::category_board, 11, TRUE),
    ('Fitness', 'en-US', 'expense', NULL, 'expense'::category_board, 12, TRUE),
    ('Entertainment', 'en-US', 'expense', NULL, 'expense'::category_board, 13, TRUE),
    ('Education', 'en-US', 'expense', NULL, 'expense'::category_board, 14, TRUE),
    ('Travel', 'en-US', 'expense', NULL, 'expense'::category_board, 15, TRUE),
    ('Pets', 'en-US', 'expense', NULL, 'expense'::category_board, 16, TRUE),
    ('Family', 'en-US', 'expense', NULL, 'expense'::category_board, 17, TRUE),
    ('Automotive', 'en-US', 'expense', NULL, 'expense'::category_board, 18, TRUE),
    ('Campus', 'en-US', 'expense', NULL, 'expense'::category_board, 19, TRUE),
    ('Gifts', 'en-US', 'expense', NULL, 'expense'::category_board, 20, TRUE),
    ('Other', 'en-US', 'expense', NULL, 'expense'::category_board, 99, TRUE),
    ('Salary', 'en-US', 'income', NULL, 'income'::category_board, 1, TRUE),
    ('Bonus', 'en-US', 'income', NULL, 'income'::category_board, 2, TRUE),
    ('Investments', 'en-US', 'income', NULL, 'income'::category_board, 3, TRUE),
    ('Part-time Work', 'en-US', 'income', NULL, 'income'::category_board, 4, TRUE),
    ('Side Business', 'en-US', 'income', NULL, 'income'::category_board, 5, TRUE),
    ('Cash Gifts', 'en-US', 'income', NULL, 'income'::category_board, 6, TRUE)
ON CONFLICT DO NOTHING;

WITH child_template(name, parent_name, group_name, default_board, sort_order) AS (
    VALUES
        ('Breakfast', 'Food', 'expense', 'expense'::category_board, 1),
        ('Lunch', 'Food', 'expense', 'expense'::category_board, 2),
        ('Dinner', 'Food', 'expense', 'expense'::category_board, 3),
        ('Coffee', 'Food', 'expense', 'expense'::category_board, 4),
        ('Delivery', 'Food', 'expense', 'expense'::category_board, 5),
        ('Dining Out', 'Food', 'expense', 'expense'::category_board, 6),
        ('Water', 'Daily Living', 'expense', 'expense'::category_board, 1),
        ('Electricity', 'Daily Living', 'expense', 'expense'::category_board, 2),
        ('Gas', 'Daily Living', 'expense', 'expense'::category_board, 3),
        ('Property Management', 'Daily Living', 'expense', 'expense'::category_board, 4),
        ('Household Supplies', 'Daily Living', 'expense', 'expense'::category_board, 5),
        ('Metro', 'Transportation', 'expense', 'expense'::category_board, 1),
        ('Bus', 'Transportation', 'expense', 'expense'::category_board, 2),
        ('Taxi', 'Transportation', 'expense', 'expense'::category_board, 3),
        ('Fuel', 'Transportation', 'expense', 'expense'::category_board, 4),
        ('Parking', 'Transportation', 'expense', 'expense'::category_board, 5),
        ('Fees', 'Finance', 'expense', 'expense'::category_board, 1),
        ('Interest Expense', 'Finance', 'expense', 'expense'::category_board, 2),
        ('Foreign Exchange Loss', 'Finance', 'expense', 'expense'::category_board, 3),
        ('Base Salary', 'Salary', 'income', 'income'::category_board, 1),
        ('Performance Bonus', 'Salary', 'income', 'income'::category_board, 2),
        ('Allowance', 'Salary', 'income', 'income'::category_board, 3),
        ('Dividends', 'Investments', 'income', 'income'::category_board, 1),
        ('Fund Returns', 'Investments', 'income', 'income'::category_board, 2),
        ('Interest', 'Investments', 'income', 'income'::category_board, 3),
        ('Capital Gains', 'Investments', 'income', 'income'::category_board, 4),
        ('Family and Friends', 'Cash Gifts', 'income', 'income'::category_board, 1),
        ('Platform Rewards', 'Cash Gifts', 'income', 'income'::category_board, 2)
)
INSERT INTO system_category_templates (
    name, locale, group_name, parent_id, default_board, sort_order, is_selectable)
SELECT child.name,
       'en-US',
       child.group_name,
       parent.id,
       child.default_board,
       child.sort_order,
       TRUE
FROM child_template AS child
JOIN system_category_templates AS parent
  ON parent.locale = 'en-US'
 AND parent.group_name = child.group_name
 AND parent.parent_id IS NULL
 AND parent.name = child.parent_name
ON CONFLICT DO NOTHING;
