use embedded_graphics::geometry::{Point, Size};
use embedded_graphics::primitives::Rectangle;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct RectSpec {
    pub x: i32,
    pub y: i32,
    pub width: u32,
    pub height: u32,
}

impl RectSpec {
    pub const fn new(x: i32, y: i32, width: u32, height: u32) -> Self {
        Self {
            x,
            y,
            width,
            height,
        }
    }

    pub fn rectangle(self) -> Rectangle {
        Rectangle::new(
            Point::new(self.x, self.y),
            Size::new(self.width, self.height),
        )
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PanelLayout {
    pub frame: RectSpec,
    pub inner: RectSpec,
    pub header: Point,
    pub value_right: i32,
    pub value_y: i32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct GraphLayout {
    pub plot: RectSpec,
    pub clear: RectSpec,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct DashboardLayout {
    pub panels: [PanelLayout; 3],
    pub graph: GraphLayout,
    pub pressure_bar: RectSpec,
    pub status: RectSpec,
    pub status_left: Point,
    pub status_right: Point,
    pub debug_origin: Point,
    pub frame_indicator: RectSpec,
}

pub const CLASSIC_LAYOUT: DashboardLayout = DashboardLayout {
    panels: [
        PanelLayout {
            frame: RectSpec::new(0, 0, 100, 80),
            inner: RectSpec::new(2, 18, 96, 60),
            header: Point::new(2, 2),
            value_right: 92,
            value_y: 25,
        },
        PanelLayout {
            frame: RectSpec::new(110, 0, 100, 80),
            inner: RectSpec::new(112, 18, 96, 60),
            header: Point::new(112, 2),
            value_right: 200,
            value_y: 30,
        },
        PanelLayout {
            frame: RectSpec::new(220, 0, 100, 80),
            inner: RectSpec::new(222, 18, 96, 60),
            header: Point::new(222, 2),
            value_right: 312,
            value_y: 25,
        },
    ],
    graph: GraphLayout {
        plot: RectSpec::new(10, 90, 265, 121),
        clear: RectSpec::new(8, 88, 268, 123),
    },
    pressure_bar: RectSpec::new(280, 90, 30, 121),
    status: RectSpec::new(0, 220, 320, 20),
    status_left: Point::new(4, 222),
    status_right: Point::new(316, 222),
    debug_origin: Point::new(10, 100),
    frame_indicator: RectSpec::new(316, 236, 4, 4),
};

pub const WORKSHOP_LAYOUT: DashboardLayout = DashboardLayout {
    panels: [
        PanelLayout {
            frame: RectSpec::new(0, 0, 104, 80),
            inner: RectSpec::new(2, 18, 100, 60),
            header: Point::new(2, 2),
            value_right: 98,
            value_y: 25,
        },
        PanelLayout {
            frame: RectSpec::new(108, 0, 104, 80),
            inner: RectSpec::new(110, 18, 100, 60),
            header: Point::new(110, 2),
            value_right: 206,
            value_y: 30,
        },
        PanelLayout {
            frame: RectSpec::new(216, 0, 104, 80),
            inner: RectSpec::new(218, 18, 100, 60),
            header: Point::new(218, 2),
            value_right: 314,
            value_y: 25,
        },
    ],
    graph: GraphLayout {
        plot: RectSpec::new(18, 92, 240, 119),
        clear: RectSpec::new(16, 90, 244, 121),
    },
    pressure_bar: RectSpec::new(266, 92, 44, 119),
    status: CLASSIC_LAYOUT.status,
    status_left: CLASSIC_LAYOUT.status_left,
    status_right: CLASSIC_LAYOUT.status_right,
    debug_origin: Point::new(18, 102),
    frame_indicator: CLASSIC_LAYOUT.frame_indicator,
};

pub const ALCHEMY_LAYOUT: DashboardLayout = DashboardLayout {
    panels: [
        PanelLayout {
            frame: RectSpec::new(229, 39, 78, 68),
            inner: RectSpec::new(233, 58, 70, 45),
            header: Point::new(250, 42),
            value_right: 300,
            value_y: 66,
        },
        PanelLayout {
            frame: RectSpec::new(141, 39, 87, 68),
            inner: RectSpec::new(145, 58, 79, 45),
            header: Point::new(158, 42),
            value_right: 220,
            value_y: 61,
        },
        PanelLayout {
            frame: RectSpec::new(52, 39, 88, 68),
            inner: RectSpec::new(56, 58, 80, 45),
            header: Point::new(63, 42),
            value_right: 132,
            value_y: 66,
        },
    ],
    graph: GraphLayout {
        plot: RectSpec::new(57, 125, 245, 76),
        clear: RectSpec::new(55, 122, 249, 82),
    },
    pressure_bar: RectSpec::new(23, 64, 16, 137),
    status: RectSpec::new(46, 212, 230, 18),
    status_left: Point::new(50, 213),
    status_right: Point::new(272, 213),
    debug_origin: Point::new(59, 127),
    frame_indicator: RectSpec::new(314, 235, 4, 4),
};

impl DashboardLayout {
    pub fn is_valid(self, screen_width: i32, screen_height: i32) -> bool {
        let inside = |rect: RectSpec| {
            rect.x >= 0
                && rect.y >= 0
                && rect.x + rect.width as i32 <= screen_width
                && rect.y + rect.height as i32 <= screen_height
        };
        self.panels
            .iter()
            .all(|panel| inside(panel.frame) && inside(panel.inner))
            && inside(self.graph.plot)
            && inside(self.graph.clear)
            && inside(self.pressure_bar)
            && inside(self.status)
            && inside(self.frame_indicator)
    }
}
