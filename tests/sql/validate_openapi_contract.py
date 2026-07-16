"""Validate the checked-in OpenAPI route and browser protocol contract."""

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
    "/api/v1/web/auth/register": {"post"},
    "/api/v1/web/auth/login": {"post"},
    "/api/v1/web/auth/refresh": {"post"},
    "/api/v1/web/auth/logout": {"post"},
    "/api/v1/currencies": {"get"},
    "/api/v1/accounts": {"get", "post"},
    "/api/v1/accounts/{accountId}/balance": {"get"},
    "/api/v1/accounts/{accountId}/archive": {"post"},
    "/api/v1/accounts/{accountId}/restore": {"post"},
    "/api/v1/accounts/{accountId}": {"get", "put", "delete"},
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
        failures.append("OpenAPI path set differs from the implemented route table")
    for path, methods in EXPECTED.items():
        actual = {
            key for key in paths.get(path, {})
            if key in {"get", "post", "put", "delete", "patch"}
        }
        if actual != methods:
            failures.append(f"{path} methods are {sorted(actual)}, expected {sorted(methods)}")
    if "delete" in paths.get("/api/v1/transfers/{transferGroupId}", {}):
        failures.append("Phase 1 must not publish a transfer delete operation")

    operation_ids: list[str] = []
    for path, path_item in paths.items():
        for method, operation in path_item.items():
            if method not in {"get", "post", "put", "delete", "patch"}:
                continue
            operation_id = operation.get("operationId")
            if not isinstance(operation_id, str) or not operation_id:
                failures.append(f"{method.upper()} {path} has no operationId")
            else:
                operation_ids.append(operation_id)
    if len(operation_ids) != len(set(operation_ids)):
        failures.append("OpenAPI operationId values must be unique")

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
    components = document.get("components", {})
    security_schemes = components.get("securitySchemes", {})
    cookie_auth = security_schemes.get("cookieAuth", {})
    if cookie_auth != {
        "type": "apiKey",
        "in": "cookie",
        "name": "pfh_refresh",
        "description": "Secure HttpOnly refresh cookie; never readable by browser JavaScript",
    }:
        failures.append("cookieAuth security scheme is incomplete")
    for path in (
        "/api/v1/web/auth/register",
        "/api/v1/web/auth/login",
        "/api/v1/web/auth/refresh",
        "/api/v1/web/auth/logout",
    ):
        serialized = json.dumps(paths.get(path, {}), sort_keys=True)
        if "refreshToken" in serialized:
            failures.append(f"{path} exposes refreshToken in its JSON contract")
        if "Set-Cookie" not in serialized or "no-store" not in serialized:
            failures.append(f"{path} does not define secure cookie response headers")

    idempotency_ref = "#/components/parameters/IdempotencyKey"
    for path in ("/api/v1/transactions", "/api/v1/transfers"):
        operation = paths.get(path, {}).get("post", {})
        parameters = operation.get("parameters", [])
        if not any(item.get("$ref") == idempotency_ref for item in parameters):
            failures.append(f"POST {path} must require Idempotency-Key")
        if "409" not in operation.get("responses", {}):
            failures.append(f"POST {path} must publish idempotency conflicts")

    error_schema = schemas.get("ErrorResponse", {})
    expected_error_fields = {
        "error_code", "message", "trace_id", "retryable", "field_errors",
    }
    if set(error_schema.get("required", [])) != expected_error_fields:
        failures.append("ErrorResponse required fields are incomplete")
    if set(error_schema.get("properties", {})) != expected_error_fields:
        failures.append("ErrorResponse properties are incomplete")
    pagination = schemas.get("CursorPageMetadata", {})
    if pagination.get("properties", {}).get("nextCursor", {}).get("maxLength") != 512:
        failures.append("CursorPageMetadata must bound nextCursor to 512 characters")
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
    generated = ROOT / "frontend/src/generated/api-types.ts"
    try:
        generated_text = generated.read_text(encoding="utf-8")
    except OSError as error:
        failures.append(f"Generated frontend API types cannot be read: {error}")
        generated_text = ""
    for marker in (
        "registerWebUser", "refreshWebSession", "Idempotency-Key",
        "field_errors", "CursorPageMetadata",
    ):
        if marker not in generated_text:
            failures.append(f"Generated frontend API types are missing {marker}")

    if failures:
        for failure in failures:
            print(f"ERROR: {failure}")
        return 1
    print("OpenAPI contract and generated types: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
