"""204套持久化测试的显式非单位四端口软件夹具，不代表硬件精度。"""

COUNT = 204
POINTS = 800


def set_name(index):
    return f"CableTest software pool {index + 1:03d} / four-port TOSM"


def coefficients(set_index, point, cell):
    """各集、频点和矩阵格均可区分；非零匹配只置于对角，便于独立核算。"""
    offset = complex(
        (set_index + 1) * 0.00001 + (cell + 1) * 0.0003 + point * 0.0000007,
        -(cell + 1) * 0.0002 - point * 0.0000003,
    )
    tracking = complex(
        1.3 + set_index * 0.001 + cell * 0.013 + point * 0.00001,
        0.02 + cell * 0.0004 + set_index * 0.00001 + point * 0.0000001,
    )
    match = (
        complex(0.01 + set_index * 0.0001, -0.002 + point * 0.00000001)
        if cell % 5 == 0
        else 0j
    )
    return offset, match, tracking


def document(identity, axis, index):
    rows = []
    for point in range(len(axis)):
        row = []
        for cell in range(16):
            for value in coefficients(index, point, cell):
                row.extend((value.real, value.imag))
        rows.append(row)
    return {
        "id": "0",
        "name": set_name(index),
        "deviceIdentity": identity,
        "method": "fourPort",
        "port1": 1,
        "port2": 4,
        "includeIsolation": True,
        "ifBandwidthHz": 1000,
        "frequencyHz": axis,
        "powerDbm": [0.0] * len(axis),
        "onePortTerms": [],
        "twoPortTerms": [],
        "fourPortTerms": rows,
    }
