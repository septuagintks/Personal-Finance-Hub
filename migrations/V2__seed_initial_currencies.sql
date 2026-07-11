-- Version: 2
-- Description: Seed initial currency metadata (fiat + controlled crypto whitelist)
-- Required for Phase 1; accounts and users reference currencies(code).

-- =============================================================================
-- Fiat currencies (ISO-4217 subset)
-- =============================================================================

INSERT INTO currencies (code, name, display_name, symbol, precision, is_crypto, is_enabled) VALUES
('USD', 'US Dollar', 'US Dollar', '$', 2, FALSE, TRUE),
('CNY', 'Chinese Yuan', 'Chinese Yuan', '¥', 2, FALSE, TRUE),
('EUR', 'Euro', 'Euro', '€', 2, FALSE, TRUE),
('GBP', 'British Pound', 'British Pound', '£', 2, FALSE, TRUE),
('JPY', 'Japanese Yen', 'Japanese Yen', '¥', 0, FALSE, TRUE),
('HKD', 'Hong Kong Dollar', 'Hong Kong Dollar', 'HK$', 2, FALSE, TRUE),
('AUD', 'Australian Dollar', 'Australian Dollar', 'A$', 2, FALSE, TRUE),
('CAD', 'Canadian Dollar', 'Canadian Dollar', 'C$', 2, FALSE, TRUE),
('CHF', 'Swiss Franc', 'Swiss Franc', 'CHF', 2, FALSE, TRUE),
('SGD', 'Singapore Dollar', 'Singapore Dollar', 'S$', 2, FALSE, TRUE),
('KRW', 'South Korean Won', 'South Korean Won', '₩', 0, FALSE, TRUE),
('INR', 'Indian Rupee', 'Indian Rupee', '₹', 2, FALSE, TRUE),
('RUB', 'Russian Ruble', 'Russian Ruble', '₽', 2, FALSE, TRUE),
('BRL', 'Brazilian Real', 'Brazilian Real', 'R$', 2, FALSE, TRUE),
('ZAR', 'South African Rand', 'South African Rand', 'R', 2, FALSE, TRUE),
('MXN', 'Mexican Peso', 'Mexican Peso', 'Mex$', 2, FALSE, TRUE),
('NZD', 'New Zealand Dollar', 'New Zealand Dollar', 'NZ$', 2, FALSE, TRUE),
('SEK', 'Swedish Krona', 'Swedish Krona', 'kr', 2, FALSE, TRUE),
('NOK', 'Norwegian Krone', 'Norwegian Krone', 'kr', 2, FALSE, TRUE),
('TWD', 'Taiwan Dollar', 'Taiwan Dollar', 'NT$', 2, FALSE, TRUE)
ON CONFLICT (code) DO NOTHING;

-- =============================================================================
-- Cryptocurrencies (controlled whitelist)
-- =============================================================================

INSERT INTO currencies (code, name, display_name, symbol, precision, is_crypto, is_enabled) VALUES
('BTC', 'Bitcoin', 'Bitcoin', '₿', 8, TRUE, TRUE),
('ETH', 'Ethereum', 'Ethereum', 'Ξ', 8, TRUE, TRUE),
('USDT', 'Tether', 'Tether', 'USDT', 8, TRUE, TRUE),
('USDC', 'USD Coin', 'USD Coin', 'USDC', 8, TRUE, TRUE),
('BNB', 'Binance Coin', 'Binance Coin', 'BNB', 8, TRUE, TRUE),
('XRP', 'Ripple', 'Ripple', 'XRP', 6, TRUE, TRUE),
('ADA', 'Cardano', 'Cardano', 'ADA', 6, TRUE, TRUE),
('DOGE', 'Dogecoin', 'Dogecoin', 'Ð', 8, TRUE, TRUE),
('SOL', 'Solana', 'Solana', 'SOL', 8, TRUE, TRUE),
('TRX', 'TRON', 'TRON', 'TRX', 6, TRUE, TRUE),
('MATIC', 'Polygon', 'Polygon', 'MATIC', 8, TRUE, TRUE),
('DOT', 'Polkadot', 'Polkadot', 'DOT', 8, TRUE, TRUE),
('WBTC', 'Wrapped Bitcoin', 'Wrapped Bitcoin', 'WBTC', 8, TRUE, TRUE)
ON CONFLICT (code) DO NOTHING;
