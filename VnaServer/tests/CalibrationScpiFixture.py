"""四端口SCPI往返验收使用的独立显式软件夹具。"""


def four_port_fixture(http, frame, raw, name):
    row = []
    expected = []
    for index, values in enumerate(raw):
        offset = complex(0.001 * (index + 1), -0.0005 * index)
        tracking = complex(1.2 + 0.04 * index, 0.01)
        row.extend([offset.real, offset.imag, 0.0, 0.0, tracking.real, tracking.imag])
        for point in range(17):
            corrected = (
                complex(*values[point * 2 : point * 2 + 2]) - offset
            ) / tracking
            expected.extend((corrected.real, corrected.imag))
    document = {
        "id": "0",
        "name": name,
        "deviceIdentity": http.status()["identity"],
        "ifBandwidthHz": 1000,
        "includeIsolation": True,
        "method": "fourPort",
        "port1": 1,
        "port2": 4,
        "frequencyHz": frame["frequencyHz"],
        "powerDbm": [0.0] * 17,
        "onePortTerms": [],
        "twoPortTerms": [],
        "fourPortTerms": [row] * 17,
    }
    return document, expected
