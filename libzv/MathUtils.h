//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace zv
{

    struct ColMajorMatrix3f
    {
        union {
            float v[3*3];
            
            // mRowCol
            struct {
                float m00, m10, m20;
                float m01, m11, m21;
                float m02, m12, m22;
            };
        };
        
        ColMajorMatrix3f() = default;
        
        // mRowCol
        ColMajorMatrix3f(float m00, float m01, float m02,
                         float m10, float m11, float m12,
                         float m20, float m21, float m22)
        : m00(m00), m01(m01), m02(m02),
          m10(m10), m11(m11), m12(m12),
          m20(m20), m21(m21), m22(m22)
        {
            
        }
    };

    inline int intRnd (float f) { return (int)std::roundf(f); };
    
    inline uint8_t saturateAndCast (float v)
    {
        return std::max (std::min(v, 255.f), 0.f);
    };

    inline uint8_t roundAndSaturateToUint8 (float v)
    {
        return (uint8_t)std::max (std::min(v + 0.5f, 255.f), 0.f);
    };

    inline double pow7 (double x)
    {
        double pow3 = x*x*x;
        return pow3*pow3*x;
    }

    inline double sqr(double x)
    {
        return x*x;
    }

    inline bool floatEquals(float a, float b, float eps = 1e-8)
    {
        return std::abs(a-b) < eps;
    }

    inline bool doubleEquals(double a, double b, double eps = 1e-10)
    {
        return std::abs(a-b) < eps;
    }

    static constexpr double Rad2Deg = 57.29577951308232;
    static constexpr double Deg2Rad = 0.017453292519943295;

    template <class T>
    T keepInRange (T value, T min, T max)
    {
        if (value < min)
            return min;
        if (value > max)
            return max;
        return value;
    }

    template <typename EnumT>
    void advanceEnum (EnumT& v, int increment, EnumT maxValue)
    {
        int newMode_asInt = (int)v;
        newMode_asInt += increment;
        if (newMode_asInt < 0)
        {
            newMode_asInt = 0;
        }
        else if (newMode_asInt == (int)maxValue)
        {
            --newMode_asInt;
        }
        
        v = (EnumT)newMode_asInt;
    }

    struct vec2i
    {
        vec2i (int x = 0, int y = 0) : x(x), y(y) {}
        
        union {
            int v[2];
            struct { int x, y; };
            struct { int col, row; };
        };
    };

    inline bool operator== (const vec2i& lhs, const vec2i& rhs){
        return lhs.x == rhs.x && lhs.y == rhs.y;
    }

    struct vec2f
    {
        vec2f (float x = 0, float y = 0) : x(x), y(y) {}
        
        union {
            float v[2];
            struct { float x, y; };
        };
    };
    struct vec4d
    {
        vec4d (double x = 0., double y = 0., double z = 0., double w = 0.) : x(x), y(y), z(z), w(w) {}
        
        union {
            double v[4];
            struct { double x, y, z, w; };
        };
    };
    struct Point
    {
        Point() = default;
        Point (double x, double y) : x(x), y(y) {}
    
        inline Point& operator-=(const Point& rhs)
        {
            x -= rhs.x;
            y -= rhs.y;
            return *this;
        }
        
        inline Point& operator+=(const Point& rhs)
        {
            x += rhs.x;
            y += rhs.y;
            return *this;
        }
        
        bool isValid() const { return !std::isnan(x) && !std::isnan(y); }

        double length() const { return std::sqrt(x*x + y*y); }

        double x = NAN;
        double y = NAN;
    };

    inline Point operator-(const Point& p1, const Point& p2)
    {
        Point p = p1; p -= p2; return p;
    }

    inline Point operator+(const Point& p1, const Point& p2)
    {
        Point p = p1; p += p2; return p;
    }

    inline Point uvToRoundedPixel (const Point& uvP, int width, int height)
    {
        return Point(int(uvP.x*width + 0.5f) / double(width),
                     int(uvP.y*height + 0.5f) / double(height));
    }

    struct Rect
    {
        Point origin;
        Point size;
        
        static Rect from_x_y_w_h (double x, double y, double w, double h)
        {
            zv::Rect r;
            r.origin.x = x;
            r.origin.y = y;
            r.size.x = w;
            r.size.y = h;
            return r;
        }

        void scale (double sx, double sy)
        {
            origin.x *= sx;
            origin.y *= sy;
            size.x *= sx;
            size.y *= sy;
        }
            
        Point topLeft() const { return origin; }
        Point topRight() const { return origin + Point(size.x, 0); }
        Point bottomRight() const { return origin + size; }
        Point bottomLeft() const { return origin + Point(0, size.y); }
        
        void moveTopLeft (Point tl)
        {
            auto br = bottomRight();
            origin.x = std::min(br.x, tl.x);
            origin.y = std::min(br.y, tl.y);
            size.x = br.x - origin.x;
            size.y = br.y - origin.y;
        }

        void moveTopRight (Point tr)
        {
            auto bl = bottomLeft();
            tr.x = std::max(tr.x, bl.x);
            tr.y = std::min(tr.y, bl.y);
            
            origin.y = tr.y;
            size.x = tr.x - bl.x;
            size.y = bl.y - tr.y;
        }

        void moveBottomRight (Point br)
        {
            auto tl = topLeft();
            br.x = std::max(tl.x, br.x);
            br.y = std::max(tl.y, br.y);
            
            size.x = br.x - tl.x;
            size.y = br.y - tl.y;
        }

        void moveBottomLeft (Point bl)
        {
            auto tr = topRight();
            bl.x = std::min(tr.x, bl.x);
            bl.y = std::max(tr.y, bl.y);
            origin.x = bl.x;
            size.x = tr.x - bl.x;
            size.y = bl.y - tr.y;
        }

        bool contains (const Point& p) const
        {
            return (p.x >= origin.x
                    && p.x < origin.x + size.x
                    && p.y >= origin.y
                    && p.y < origin.y + size.y);
        }

        zv::Rect intersect (const zv::Rect& rhs) const
        {
            zv::Rect intersection;
            intersection.origin.x = std::max (origin.x, rhs.origin.x);
            intersection.origin.y = std::max (origin.y, rhs.origin.y);
            intersection.size.x = std::min (origin.x + size.x, rhs.origin.x + rhs.size.x) - intersection.origin.x;
            intersection.size.y = std::min (origin.y + size.y, rhs.origin.y + rhs.size.y) - intersection.origin.y;
            if (intersection.size.x < 0.)  intersection.size.x = 0.;
            if (intersection.size.y < 0.)  intersection.size.y = 0.;
            return intersection;
        }

        double area () const { return size.x * size.y; }
        
        Rect& operator*= (double s)
        {
            origin.x *= s;
            origin.y *= s;
            size.x *= s;
            size.y *= s;
            return *this;
        }
    };

    struct Line
    {
        Line () = default;
        Line (const Point& p1, const Point& p2) : p1(p1), p2(p2) {}

        void scale (double sx, double sy)
        {
            p1.x *= sx;
            p1.y *= sy;
            p2.x *= sx;
            p2.y *= sy;
        }

        Point p1;
        Point p2;
    };

} // zv
