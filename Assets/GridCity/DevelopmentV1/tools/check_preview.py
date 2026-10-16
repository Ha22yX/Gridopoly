"""Exercise the local gallery with Playwright; screenshots go to a chosen audit folder."""
import argparse
import json
from pathlib import Path
from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--output", required=True)
parser.add_argument("--channel", default="msedge")
args = parser.parse_args()
output = Path(args.output)
output.mkdir(parents=True, exist_ok=True)
with sync_playwright() as p:
    browser = p.chromium.launch(channel=args.channel, headless=True)
    page = browser.new_page(viewport={"width": 1280, "height": 900}, device_scale_factor=1)
    errors = []
    page.on("pageerror", lambda error: errors.append(str(error)))
    page.goto((ROOT / "review/index.html").as_uri())
    assert page.locator("article").count() == 22
    checked = 0
    for level in range(6):
        page.locator(f'[data-level="{level}"]').click()
        page.evaluate("document.querySelectorAll('img').forEach(i=>i.loading='eager')")
        page.wait_for_function("Array.from(document.images).every(i=>i.complete && i.naturalWidth === 160)")
        assert page.locator('[data-level][aria-pressed="true"]').count() == 1
        assert page.locator(".state").all_text_contents() == [["空地", "1栋房屋", "2栋房屋", "3栋房屋", "4栋房屋", "1座旅馆"][level]] * 22
        checked += page.locator("article img").count()
    page.locator("#district").select_option("C")
    assert page.locator("article:visible").count() == 3
    page.locator("#actual").click()
    assert page.locator("article:visible img").first.bounding_box()["width"] == 160
    page.locator("#actual").click()
    page.locator("#district").select_option("all")
    page.screenshot(path=str(output / "gallery-desktop.png"), full_page=True)
    page.set_viewport_size({"width": 390, "height": 844})
    assert page.evaluate("document.documentElement.scrollWidth <= innerWidth")
    page.screenshot(path=str(output / "gallery-mobile.png"), full_page=True)
    page.locator("#actual").click()
    assert page.evaluate("document.documentElement.scrollWidth <= innerWidth")
    assert not errors, errors
    browser.close()
print(json.dumps(dict(image_states_loaded=checked, desktop_width=1280, mobile_width=390,
                      district_filter=True, actual_size=True, browser_errors=errors)))
