"""Validate the checked-in Phase 1 OpenAPI contract with stdlib only."""

from pathlib import Path
import json
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
CONTRACT = ROOT / "Docs/Architecture/10_REST_API_OpenAPI.json"
ADAPTER = ROOT / "src/presentation/drogon_http_adapter.cpp"


EXPECTED = {
    "/api/v1/auth/register": {"post"},
    "/api/v1/auth/login": {"post"},
    "/api/v1/auth/refresh": {"post"},
    "/api/v1/auth/logout": {"post"},
    "/api/v1/currencies": {"get"},
    "/api/v1/accounts": {"get", "post"},
    "/api/v1/accounts/{accountId}/balance": {"get"},
    "/api/v1/accounts/{accountId}/archive": {"post"},
    "/api/v1/accounts/{accountId}": {"delete"},
    "/api/v1/categories": {"get", "post"},
    "/api/v1/categories/{categoryId}": {"delete"},
    "/api/v1/tags": {"get", "post"},
    "/api/v1/tags/{tagId}": {"delete"},
    "/api/v1/users/me/preferences": {"get", "put"},
    "/api/v1/transactions": {"post"},
    "/api/v1/transactions/{transactionId}": {"delete"},
    "/api/v1/transactions/{transactionId}/tags": {"put"},
    "/api/v1/transfers": {"post"},
    "/api/v1/transfers/{transferGroupId}": {"get"},
    "/api/v1/reports/net-worth": {"get"},
    "/api/v1/reports/cash-flow": {"get"},
    "/api/v1/reports/dashboard-summary": {"get"},
}

ADAPTER_EXPECTED = {
    path.replace("{accountId}", "{1}")
        .replace("{categoryId}", "{1}")
        .replace("{tagId}", "{1}")
        .replace("{transactionId}", "{1}")
        .replace("{transferGroupId}", "{1}"): methods
    for path, methods in EXPECTED.items()
}


def main() -> int:
    try:
        document = json.loads(CONTRACT.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"ERROR: OpenAPI document cannot be parsed: {error}")
        return 1

    failures: list[str] = []
    if document.get("openapi") != "3.1.0":
        failures.append("OpenAPI version must be 3.1.0")
    paths = document.get("paths", {})
    if set(paths) != set(EXPECTED):
        failures.append("OpenAPI path set differs from the Phase 1 route table")
    for path, methods in EXPECTED.items():
        actual = {
            key for key in paths.get(path, {})
            if key in {"get", "post", "put", "delete", "patch"}
        }
        if actual != methods:
            failures.append(f"{path} methods are {sorted(actual)}, expected {sorted(methods)}")
    if "delete" in paths.get("/api/v1/transfers/{transferGroupId}", {}):
        failures.append("Phase 1 must not publish a transfer delete operation")

    try:
        adapter_source = ADAPTER.read_text(encoding="utf-8")
    except OSError as error:
        failures.append(f"Drogon route adapter cannot be read: {error}")
        adapter_source = ""
    registered: dict[str, set[str]] = {}
    route_pattern = re.compile(
        r'register_(?:static|dynamic)\(\s*"([^"]+)"\s*,\s*'
        r'HttpMethod::(Get|Post|Put|Delete)'
    )
    for path, method in route_pattern.findall(adapter_source):
        registered.setdefault(path, set()).add(method.lower())
    if registered != ADAPTER_EXPECTED:
        failures.append(
            "Drogon route registration differs from the checked-in OpenAPI route table"
        )
    if '"/api/v1/transfers/{1}", HttpMethod::Delete' in adapter_source:
        failures.append("Drogon adapter must not register transfer deletion")
    if 'response.headers.insert_or_assign("X-Trace-Id", trace_id)' not in adapter_source:
        failures.append("Drogon exception responses must expose their trace id header")

    schemas = document.get("components", {}).get("schemas", {})
    decimal = schemas.get("DecimalString", {})
    positive = schemas.get("PositiveDecimalString", {})
    if decimal.get("type") != "string" or positive.get("type") != "string":
        failures.append("Decimal schemas must use JSON strings")
    if decimal.get("maxLength") != 128 or positive.get("maxLength") != 128:
        failures.append("Decimal request schemas must enforce the 128-byte boundary")
    if decimal.get("pattern") != r"^-?[0-9]+(?:\.[0-9]+)?$":
        failures.append("DecimalString must use the plain signed-decimal grammar")
    if positive.get("pattern") != r"^[0-9]+(?:\.[0-9]+)?$":
        failures.append("PositiveDecimalString must use the plain magnitude grammar")
    for schema_name in ("CreateTransactionRequest", "Transaction"):
        amount = schemas.get(schema_name, {}).get("properties", {}).get("amount", {})
        if amount.get("$ref") != "#/components/schemas/DecimalString":
            failures.append(f"{schema_name}.amount must reference DecimalString")
    for schema_name in ("CreateTransferRequest", "Transfer"):
        serialized = json.dumps(schemas.get(schema_name, {}), sort_keys=True)
        if "number" in serialized:
            failures.append(f"{schema_name} contains a JSON number monetary schema")

    closed_objects = {
        "RegisterResponse": {
            "userId", "accessToken", "refreshToken", "expiresIn", "tokenType",
        },
        "CategoryTree": {
            "id", "name", "board", "source", "parentId", "templateId",
            "sortOrder", "children",
        },
    }
    for schema_name, expected_properties in closed_objects.items():
        schema = schemas.get(schema_name, {})
        if schema.get("type") != "object":
            failures.append(f"{schema_name} must be a concrete object schema")
        if schema.get("additionalProperties") is not False:
            failures.append(f"{schema_name} must reject unknown properties")
        if "allOf" in schema:
            failures.append(
                f"{schema_name} must not extend a closed object with allOf"
            )
        if set(schema.get("properties", {})) != expected_properties:
            failures.append(f"{schema_name} property set is incomplete")
        if set(schema.get("required", [])) != expected_properties:
            failures.append(f"{schema_name} required property set is incomplete")

    locale = schemas.get("LocaleTag", {})
    if locale.get("type") != "string" or locale.get("maxLength") != 16:
        failures.append("LocaleTag must be a string of at most 16 characters")
    if locale.get("pattern") != r"^[A-Za-z0-9]+(?:-[A-Za-z0-9]+)*$":
        failures.append("LocaleTag grammar differs from Application validation")
    preferred_locale = (
        schemas.get("RegisterRequest", {})
        .get("properties", {})
        .get("preferredLocale", {})
    )
    if not any(
        option.get("$ref") == "#/components/schemas/LocaleTag"
        for option in preferred_locale.get("oneOf", [])
    ):
        failures.append("RegisterRequest.preferredLocale must reference LocaleTag")
    preference_locale = (
        schemas.get("UserPreference", {})
        .get("properties", {})
        .get("locale", {})
    )
    if preference_locale.get("$ref") != "#/components/schemas/LocaleTag":
        failures.append("UserPreference.locale must reference LocaleTag")

    def allows_null(schema: dict) -> bool:
        raw_type = schema.get("type")
        if raw_type == "null" or (
            isinstance(raw_type, list) and "null" in raw_type
        ):
            return True
        return any(allows_null(option) for option in schema.get("oneOf", []))

    nullable_request_fields = {
        "RegisterRequest": ("baseCurrency", "preferredLocale"),
        "CreateAccountRequest": ("description",),
        "CreateCategoryRequest": ("board", "name", "parentId", "templateId"),
        "CreateTransactionRequest": ("categoryId", "description", "occurredAt"),
        "CreateTransferRequest": (
            "outgoingAmount", "incomingAmount", "rate", "feeAmount",
            "feeSource", "feeAccountId", "description", "occurredAt",
        ),
    }
    for schema_name, fields in nullable_request_fields.items():
        properties = schemas.get(schema_name, {}).get("properties", {})
        for field in fields:
            if not allows_null(properties.get(field, {})):
                failures.append(
                    f"{schema_name}.{field} must accept explicit null like the parser"
                )

    if failures:
        for failure in failures:
            print(f"ERROR: {failure}")
        return 1
    print("OpenAPI Phase 1 contract: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
