USER_SETTINGS_FILE = "/userdata/Orbit12Orion/user_settings"

### VALUE HELPERS ###


def clamp_float(value, minimum, maximum):
    if value < minimum:
        return minimum
    if value > maximum:
        return maximum
    return value


### USER SETTINGS IO ###

def load_user_settings(neo_pixels, filename=USER_SETTINGS_FILE):
    try:
        file = open(filename, "r")
    except OSError:
        save_user_settings(neo_pixels, filename)
        return False

    with file:
        for line in file:
            parts = line.strip().split(",", 1)
            if len(parts) != 2:
                continue

            name = parts[0]
            value = parts[1]
            try:
                if name == "key_brightness":
                    neo_pixels.brightness_multiplier = clamp_float(float(value), 0, 1)
            except ValueError:
                pass

    return True


def save_user_settings(neo_pixels, filename=USER_SETTINGS_FILE):
    try:
        with open(filename, "w") as file:
            file.write("key_brightness,")
            file.write(str(neo_pixels.brightness_multiplier))
            file.write("\n")
    except OSError:
        return False
    return True
