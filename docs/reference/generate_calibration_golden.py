#!/usr/bin/env python3
"""REFERENCE ORACLE：生成并校验传统同轴数学的合成回归向量。

公式转录的固定来源是 Keysight 官方 DAM PDF，literature
5989-4840EN，SourceId=SRC-KEYSIGHT-5989-4840EN-20260329，
SHA-256=cb890ea940c6a1fd9c678513dd63ea91ae96b4d993929c2735299c80d1615d1e，
11,071,892 bytes / 68 PDF 页。terminated-line 式 (10) 位于 PDF 8，
loss/alternate-unit 语义位于 PDF 9–11（PDF 11 明确 one-way loss），
traditional coaxial 上下文位于 PDF 54–56，式 (43) 位于 PDF 56。

本脚本刻意使用 ABCD + 射影阻抗路径，不复用反射域 helper；
这只能检查同一数学转录的实现一致性，不是来源独立的证据。
输入系数是 VnaPro 合成数据，不是任何套件的厂家证书值。

本脚本不读取、固定或验证官方 blob/manifest。因此脚本 exit 0 或向量
匹配都不得单独形成 Gate/Assertion 或 CalSet 发布证据。
`KeysightAN1287TraditionalCoaxV1` 只能由外部 ProfileEvidenceGate 在匹配
blob/hash/manifest/精确页码转录、授权边界、独立 oracle 与完整 Gate
都验证通过时条件解禁；任一缺失或 hash 漂移必须 fail-closed。
"""

from decimal import Decimal, getcontext


getcontext().prec = 80
D = Decimal
SOURCE_ID = "SRC-KEYSIGHT-5989-4840EN-20260329"
SOURCE_SHA256 = "cb890ea940c6a1fd9c678513dd63ea91ae96b4d993929c2735299c80d1615d1e"
SOURCE_SIZE_BYTES = 11_071_892
SOURCE_PDF_PAGES = 68
SOURCE_EVIDENCE_STATUS = "REVIEWED_TRANSCRIPTION_EXTERNAL_MANIFEST_REQUIRED"
IMPORT_PROFILE = "KeysightAN1287TraditionalCoaxV1"
IMPORT_PROFILE_STATUS = "CONDITIONAL_FAIL_CLOSED_ON_PROFILE_EVIDENCE_GATE"
SCRIPT_OUTPUT_GATE_ELIGIBLE = False
PI = D("3.141592653589793238462643383279502884197169399375105820974944592307816406")
TWO = D(2)
TWO_PI = TWO * PI
EPS = D("1e-72")
Complex = tuple[Decimal, Decimal]


def cadd(a: Complex, b: Complex) -> Complex:
    return a[0] + b[0], a[1] + b[1]


def csub(a: Complex, b: Complex) -> Complex:
    return a[0] - b[0], a[1] - b[1]


def cmul(a: Complex, b: Complex) -> Complex:
    return a[0] * b[0] - a[1] * b[1], a[0] * b[1] + a[1] * b[0]


def cscale(a: Complex, x: Decimal) -> Complex:
    return a[0] * x, a[1] * x


def cdiv(a: Complex, b: Complex) -> Complex:
    den = b[0] * b[0] + b[1] * b[1]
    return (a[0] * b[0] + a[1] * b[1]) / den, (a[1] * b[0] - a[0] * b[1]) / den


def sin_cos(x: Decimal) -> tuple[Decimal, Decimal]:
    """Decimal Taylor oracle；本向量相位已在小范围，仍先规范到 [-pi, pi]。"""
    x %= TWO_PI
    if x > PI:
        x -= TWO_PI
    x2 = x * x
    sin_value = x
    sin_term = x
    cos_value = D(1)
    cos_term = D(1)
    n = 1
    while True:
        sin_term *= -x2 / D((2 * n) * (2 * n + 1))
        cos_term *= -x2 / D((2 * n - 1) * (2 * n))
        sin_value += sin_term
        cos_value += cos_term
        if abs(sin_term) < EPS and abs(cos_term) < EPS:
            return sin_value, cos_value
        n += 1


def sinh_cosh(z: Complex) -> tuple[Complex, Complex]:
    a, b = z
    exp_a = a.exp()
    exp_neg_a = (-a).exp()
    sinh_a = (exp_a - exp_neg_a) / TWO
    cosh_a = (exp_a + exp_neg_a) / TWO
    sin_b, cos_b = sin_cos(b)
    sinh_z = sinh_a * cos_b, cosh_a * sin_b
    cosh_z = cosh_a * cos_b, sinh_a * sin_b
    return sinh_z, cosh_z


def horner(coefficients: tuple[Decimal, ...], frequency_hz: Decimal) -> Decimal:
    value = D(0)
    for coefficient in reversed(coefficients):
        value = value * frequency_hz + coefficient
    return value


def standard_gamma(
    kind: str,
    frequency_hz: Decimal,
    coefficients: tuple[Decimal, ...],
    offset_delay_s: Decimal,
    offset_loss_ohm_per_s: Decimal,
    offset_z0_ohm: Decimal = D(50),
    reference_z0_ohm: Decimal = D(50),
) -> Complex:
    omega = TWO_PI * frequency_hz
    terminal_value = horner(coefficients, frequency_hz)
    if kind == "open":
        terminal_n = D(1), D(0)
        terminal_d = D(0), omega * terminal_value
    elif kind == "short":
        terminal_n = D(0), omega * terminal_value
        terminal_d = D(1), D(0)
    else:
        raise ValueError(f"unsupported standard kind: {kind}")

    normalized_frequency = (frequency_hz / D("1e9")).sqrt()
    alpha_l = (
        offset_loss_ohm_per_s * offset_delay_s / (TWO * offset_z0_ohm)
    ) * normalized_frequency
    beta_l = TWO_PI * frequency_hz * offset_delay_s + alpha_l
    gamma_l = alpha_l, beta_l
    zc = (
        offset_z0_ohm + offset_loss_ohm_per_s * normalized_frequency / (D(4) * PI * frequency_hz),
        -offset_loss_ohm_per_s * normalized_frequency / (D(4) * PI * frequency_hz),
    )

    sinh_g, cosh_g = sinh_cosh(gamma_l)
    abcd_a = cosh_g
    abcd_b = cmul(zc, sinh_g)
    abcd_c = cdiv(sinh_g, zc)
    abcd_d = cosh_g

    input_n = cadd(cmul(abcd_a, terminal_n), cmul(abcd_b, terminal_d))
    input_d = cadd(cmul(abcd_c, terminal_n), cmul(abcd_d, terminal_d))
    zref_d = cscale(input_d, reference_z0_ohm)
    return cdiv(csub(input_n, zref_d), cadd(input_n, zref_d))


OPEN_C = tuple(map(D, ("-0.11e-15", "6e-27", "-4.39e-36", "0.179e-45")))
SHORT_L = tuple(map(D, ("4.645e-12", "-331e-24", "10.8e-33", "-0.12e-42")))
CASES = (
    ("open", D("1e9"), OPEN_C, D("3.0765e-11"), D("1.8e9"), D("0.926184584918250"), D("-0.376997627319937")),
    ("short", D("1e9"), SHORT_L, D("3.0508e-11"), D("1.8e9"), D("-0.924172023208884"), D("0.376251276041879")),
    ("open", D("1e10"), OPEN_C, D("3.0765e-11"), D("1.8e9"), D("-0.744373990342976"), D("0.661613521308266")),
    ("short", D("1e10"), SHORT_L, D("3.0508e-11"), D("1.8e9"), D("0.762121085010137"), D("-0.642980849276044")),
)


def main() -> None:
    print(f"required_source_id={SOURCE_ID}")
    print(f"required_source_sha256={SOURCE_SHA256}")
    print(f"required_source_size_bytes={SOURCE_SIZE_BYTES}")
    print(f"required_source_pdf_pages={SOURCE_PDF_PAGES}")
    print(f"source_evidence_status={SOURCE_EVIDENCE_STATUS}")
    print(f"import_profile={IMPORT_PROFILE}")
    print(f"import_profile_status={IMPORT_PROFILE_STATUS}")
    print(f"script_output_gate_eligible={str(SCRIPT_OUTPUT_GATE_ELIGIBLE).lower()}")
    print("result_semantics=ARITHMETIC_REGRESSION_REQUIRES_EXTERNAL_MANIFEST_ORACLE_AND_GATE")

    tolerance = D("5e-13")
    for kind, frequency, coefficients, delay, loss, expected_re, expected_im in CASES:
        actual_re, actual_im = standard_gamma(kind, frequency, coefficients, delay, loss)
        if abs(actual_re - expected_re) > tolerance:
            raise RuntimeError(
                f"reference-vector mismatch ({kind}, {frequency} Hz, real): "
                f"expected={expected_re}, actual={actual_re}, tolerance={tolerance}"
            )
        if abs(actual_im - expected_im) > tolerance:
            raise RuntimeError(
                f"reference-vector mismatch ({kind}, {frequency} Hz, imag): "
                f"expected={expected_im}, actual={actual_im}, tolerance={tolerance}"
            )
        print(
            f"reference_vector {kind:5s} {frequency:.0E} Hz  "
            f"Gamma={actual_re:.15f}{actual_im:+.15f}j"
        )


if __name__ == "__main__":
    main()
