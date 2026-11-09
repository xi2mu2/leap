#include "FormulaCheatSheet.h"
#include <QCryptographicHash>
#include <QStandardPaths>
#include <QCoreApplication>

// ============================================================
// LaTeX 公式对照表数据
// 使用 abc 等代替变量，确保通用性
// ============================================================

QVector<FormulaCategory> FormulaCheatSheet::getCategories() {
    QVector<FormulaCategory> cats;

    // ---------- 1. 基础运算 ----------
    {
        FormulaCategory cat;
        cat.id = "basic";
        cat.title = "基础运算";
        cat.entries = {
                       {"分数", "\\frac{a}{b}", "basic", "普通分数"},
                       {"连分数", "\\frac{1}{a+\\frac{1}{b+\\frac{1}{c}}}", "basic", "嵌套分数"},
                       {"平方根", "\\sqrt{a}", "basic", "二次根号"},
                       {"n次根", "\\sqrt[n]{a}", "basic", "n次方根"},
                       {"上标", "a^{b}", "basic", "幂次"},
                       {"下标", "a_{b}", "basic", "下标索引"},
                       {"上下标", "a_{b}^{c}", "basic", "同时上下标"},
                       {"绝对值", "\\left| a \\right|", "basic", "绝对值符号"},
                       {"括号", "\\left( a \\right)", "basic", "自适应大小括号"},
                       {"中括号", "\\left[ a \\right]", "basic", "自适应中括号"},
                       {"花括号", "\\left\\{ a \\right\\} ", "basic", "自适应花括号"},
                       {"尖括号", "\\langle a \\rangle", "basic", "尖括号"},
                       {"取整", "\\lfloor a \\rfloor", "basic", "向下取整"},
                       {"取顶", "\\lceil a \\rceil", "basic", "向上取整"},
                       {"省略号低", "a_{1}, a_{2}, \\ldots, a_{n}", "basic", "底线省略号"},
                       {"省略号中", "a_{1} + a_{2} + \\cdots + a_{n}", "basic", "中线省略号"},
                       {"省略号竖", "\\vdots", "basic", "竖向省略号"},
                       {"对角省略", "\\ddots", "basic", "对角省略号"},
                       };
        cats.append(cat);
    }

    // ---------- 2. 希腊字母 ----------
    {
        FormulaCategory cat;
        cat.id = "greek";
        cat.title = "希腊字母";
        cat.entries = {
                       {"Alpha", "\\alpha", "greek", "小写 alpha"},
                       {"Beta", "\\beta", "greek", "小写 beta"},
                       {"Gamma", "\\gamma", "greek", "小写 gamma"},
                       {"Delta", "\\delta", "greek", "小写 delta"},
                       {"Epsilon", "\\epsilon", "greek", "小写 epsilon"},
                       {"Zeta", "\\zeta", "greek", "小写 zeta"},
                       {"Eta", "\\eta", "greek", "小写 eta"},
                       {"Theta", "\\theta", "greek", "小写 theta"},
                       {"Iota", "\\iota", "greek", "小写 iota"},
                       {"Kappa", "\\kappa", "greek", "小写 kappa"},
                       {"Lambda", "\\lambda", "greek", "小写 lambda"},
                       {"Mu", "\\mu", "greek", "小写 mu"},
                       {"Nu", "\\nu", "greek", "小写 nu"},
                       {"Xi", "\\xi", "greek", "小写 xi"},
                       {"Pi", "\\pi", "greek", "小写 pi"},
                       {"Rho", "\\rho", "greek", "小写 rho"},
                       {"Sigma", "\\sigma", "greek", "小写 sigma"},
                       {"Tau", "\\tau", "greek", "小写 tau"},
                       {"Upsilon", "\\upsilon", "greek", "小写 upsilon"},
                       {"Phi", "\\phi", "greek", "小写 phi"},
                       {"Chi", "\\chi", "greek", "小写 chi"},
                       {"Psi", "\\psi", "greek", "小写 psi"},
                       {"Omega", "\\omega", "greek", "小写 omega"},
                       {"大写 Gamma", "\\Gamma", "greek", "大写 Gamma"},
                       {"大写 Delta", "\\Delta", "greek", "大写 Delta"},
                       {"大写 Theta", "\\Theta", "greek", "大写 Theta"},
                       {"大写 Lambda", "\\Lambda", "greek", "大写 Lambda"},
                       {"大写 Xi", "\\Xi", "greek", "大写 Xi"},
                       {"大写 Pi", "\\Pi", "greek", "大写 Pi"},
                       {"大写 Sigma", "\\Sigma", "greek", "大写 Sigma"},
                       {"大写 Phi", "\\Phi", "greek", "大写 Phi"},
                       {"大写 Psi", "\\Psi", "greek", "大写 Psi"},
                       {"大写 Omega", "\\Omega", "greek", "大写 Omega"},
                       };
        cats.append(cat);
    }

    // ---------- 3. 关系符号 ----------
    {
        FormulaCategory cat;
        cat.id = "relations";
        cat.title = "关系符号";
        cat.entries = {
                       {"等于", "a = b", "relations", "等于号"},
                       {"不等于", "a \\neq b", "relations", "不等于"},
                       {"约等于", "a \\approx b", "relations", "约等于"},
                       {"相似", "a \\sim b", "relations", "相似"},
                       {"全等", "a \\cong b", "relations", "全等"},
                       {"等价", "a \\equiv b", "relations", "等价"},
                       {"小于", "a < b", "relations", "小于"},
                       {"大于", "a > b", "relations", "大于"},
                       {"小于等于", "a \\leq b", "relations", "小于等于"},
                       {"大于等于", "a \\geq b", "relations", "大于等于"},
                       {"远小于", "a \\ll b", "relations", "远小于"},
                       {"远大于", "a \\gg b", "relations", "远大于"},
                       {"属于", "a \\in b", "relations", "属于"},
                       {"不属于", "a \\notin b", "relations", "不属于"},
                       {"包含于", "a \\subseteq b", "relations", "包含于"},
                       {"真包含于", "a \\subset b", "relations", "真包含于"},
                       {"包含", "a \\supseteq b", "relations", "包含"},
                       {"真包含", "a \\supset b", "relations", "真包含"},
                       {"并集", "a \\cup b", "relations", "并集"},
                       {"交集", "a \\cap b", "relations", "交集"},
                       {"空集", "\\emptyset", "relations", "空集"},
                       {"存在", "\\exists", "relations", "存在量词"},
                       {"任意", "\\forall", "relations", "全称量词"},
                       {"蕴含", "a \\Rightarrow b", "relations", "蕴含"},
                       {"等价推出", "a \\Leftrightarrow b", "relations", "等价推出"},
                       {"因为", "\\because", "relations", "因为"},
                       {"所以", "\\therefore", "relations", "所以"},
                       };
        cats.append(cat);
    }

    // ---------- 4. 运算符号 ----------
    {
        FormulaCategory cat;
        cat.id = "operators";
        cat.title = "运算符号";
        cat.entries = {
                       {"加减", "a \\pm b", "operators", "正负号"},
                       {"减加", "a \\mp b", "operators", "负正号"},
                       {"乘", "a \\times b", "operators", "乘号"},
                       {"点乘", "a \\cdot b", "operators", "点乘"},
                       {"除", "a \\div b", "operators", "除号"},
                       {"星乘", "a \\star b", "operators", "星号"},
                       {"叉乘", "a \\ast b", "operators", "星号乘"},
                       {"偏导", "\\partial a", "operators", "偏微分符号"},
                       {"无穷", "\\infty", "operators", "无穷大"},
                       {"梯度", "\\nabla a", "operators", "梯度算子"},
                       {"积分", "\\int_{a}^{b}", "operators", "定积分"},
                       {"二重积分", "\\iint_{D}", "operators", "二重积分"},
                       {"三重积分", "\\iiint_{V}", "operators", "三重积分"},
                       {"环路积分", "\\oint_{C}", "operators", "环路积分"},
                       {"求和", "\\sum_{i=1}^{n}", "operators", "求和符号"},
                       {"求积", "\\prod_{i=1}^{n}", "operators", "求积符号"},
                       {"极限", "\\lim_{x \\to a}", "operators", "极限"},
                       {"上极限", "\\limsup_{x \\to a}", "operators", "上极限"},
                       {"下极限", "\\liminf_{x \\to a}", "operators", "下极限"},
                       };
        cats.append(cat);
    }

    // ---------- 5. 箭头 ----------
    {
        FormulaCategory cat;
        cat.id = "arrows";
        cat.title = "箭头";
        cat.entries = {
                       {"左箭头", "\\leftarrow", "arrows", "左箭头"},
                       {"右箭头", "\\rightarrow", "arrows", "右箭头"},
                       {"双向箭头", "\\leftrightarrow", "arrows", "双向箭头"},
                       {"长右箭头", "\\longrightarrow", "arrows", "长右箭头"},
                       {"映射箭头", "\\mapsto", "arrows", "映射箭头"},
                       {"上箭头", "\\uparrow", "arrows", "上箭头"},
                       {"下箭头", "\\downarrow", "arrows", "下箭头"},
                       {"左上箭头", "\\nwarrow", "arrows", "左上箭头"},
                       {"右上箭头", "\\nearrow", "arrows", "右上箭头"},
                       {"左下箭头", "\\swarrow", "arrows", "左下箭头"},
                       {"右下箭头", "\\searrow", "arrows", "右下箭头"},
                       {"双右箭头", "\\Rightarrow", "arrows", "双右箭头"},
                       {"双左箭头", "\\Leftarrow", "arrows", "双左箭头"},
                       {"双双向箭头", "\\Leftrightarrow", "arrows", "双双向箭头"},
                       {"推出", "\\Longrightarrow", "arrows", "推出（长双箭头）"},
                       {"等价", "\\Longleftrightarrow", "arrows", "等价（长双双向）"},
                       {"箭头上下", "\\updownarrow", "arrows", "上下箭头"},
                       };
        cats.append(cat);
    }

    // ---------- 6. 微积分 ----------
    {
        FormulaCategory cat;
        cat.id = "calculus";
        cat.title = "微积分";
        cat.entries = {
                       {"导数", "\\frac{dy}{dx}", "calculus", "导数"},
                       {"二阶导", "\\frac{d^{2}y}{dx^{2}}", "calculus", "二阶导数"},
                       {"偏导", "\\frac{\\partial f}{\\partial x}", "calculus", "偏导数"},
                       {"混合偏导", "\\frac{\\partial^{2}f}{\\partial x \\partial y}", "calculus", "混合偏导"},
                       {"积分", "\\int_{a}^{b} f(x) \\, dx", "calculus", "定积分"},
                       {"不定积分", "\\int f(x) \\, dx", "calculus", "不定积分"},
                       {"重积分", "\\iint_{D} f(x,y) \\, dxdy", "calculus", "二重积分"},
                       {"三重积分", "\\iiint_{V} f(x,y,z) \\, dxdydz", "calculus", "三重积分"},
                       {"曲线积分", "\\oint_{C} f \\, ds", "calculus", "第一类曲线积分"},
                       {"曲面积分", "\\iint_{S} f \\, dS", "calculus", "第一类曲面积分"},
                       {"斯托克斯", "\\oint_{C} \\mathbf{F} \\cdot d\\mathbf{r} = \\iint_{S} (\\nabla \\times \\mathbf{F}) \\cdot d\\mathbf{S}", "calculus", "斯托克斯公式"},
                       {"高斯公式", "\\iiint_{V} (\\nabla \\cdot \\mathbf{F}) \\, dV = \\iint_{S} \\mathbf{F} \\cdot d\\mathbf{S}", "calculus", "散度定理"},
                       {"格林公式", "\\oint_{C} P \\, dx + Q \\, dy = \\iint_{D} \\left( \\frac{\\partial Q}{\\partial x} - \\frac{\\partial P}{\\partial y} \\right) \\, dxdy", "calculus", "格林公式"},
                       {"泰勒展开", "f(x) = f(a) + f'(a)(x-a) + \\frac{f''(a)}{2!}(x-a)^{2} + \\cdots", "calculus", "泰勒级数"},
                       {"麦克劳林", "f(x) = f(0) + f'(0)x + \\frac{f''(0)}{2!}x^{2} + \\cdots", "calculus", "麦克劳林级数"},
                       {"牛顿-莱布尼茨", "\\int_{a}^{b} f(x) \\, dx = F(b) - F(a)", "calculus", "微积分基本定理"},
                       {"分部积分", "\\int u \\, dv = uv - \\int v \\, du", "calculus", "分部积分公式"},
                       {"变限积分", "\\frac{d}{dx} \\int_{a(x)}^{b(x)} f(t) \\, dt = f(b(x))b'(x) - f(a(x))a'(x)", "calculus", "变限积分求导"},
                       };
        cats.append(cat);
    }

    // ---------- 7. 矩阵与行列式 ----------
    {
        FormulaCategory cat;
        cat.id = "matrix";
        cat.title = "矩阵与行列式";
        cat.entries = {
                       {"2x2矩阵", "\\begin{pmatrix} a & b \\\\ c & d \\end{pmatrix}", "matrix", "2x2 矩阵"},
                       {"3x3矩阵", "\\begin{pmatrix} a & b & c \\\\ d & e & f \\\\ g & h & i \\end{pmatrix}", "matrix", "3x3 矩阵"},
                       {"2x2行列式", "\\begin{vmatrix} a & b \\\\ c & d \\end{vmatrix}", "matrix", "2x2 行列式"},
                       {"3x3行列式", "\\begin{vmatrix} a & b & c \\\\ d & e & f \\\\ g & h & i \\end{vmatrix}", "matrix", "3x3 行列式"},
                       {"矩阵转置", "\\mathbf{A}^{\\top}", "matrix", "转置"},
                       {"矩阵逆", "\\mathbf{A}^{-1}", "matrix", "逆矩阵"},
                       {"单位矩阵", "\\mathbf{I}_{n}", "matrix", "单位矩阵"},
                       {"零矩阵", "\\mathbf{0}", "matrix", "零矩阵"},
                       {"矩阵乘法", "\\mathbf{C} = \\mathbf{A} \\mathbf{B}", "matrix", "矩阵乘法"},
                       {"迹", "\\text{tr}(\\mathbf{A})", "matrix", "矩阵的迹"},
                       {"行列式", "\\det(\\mathbf{A})", "matrix", "行列式"},
                       {"秩", "\\text{rank}(\\mathbf{A})", "matrix", "矩阵的秩"},
                       {"特征值", "\\mathbf{A}\\mathbf{x} = \\lambda \\mathbf{x}", "matrix", "特征方程"},
                       {"范数", "\\left|\\left| \\mathbf{x} \\right|\\right|_{2}", "matrix", "L2范数"},
                       {"向量点积", "\\mathbf{a} \\cdot \\mathbf{b}", "matrix", "向量点积"},
                       {"向量叉积", "\\mathbf{a} \\times \\mathbf{b}", "matrix", "向量叉积"},
                       {"克罗内克积", "\\mathbf{A} \\otimes \\mathbf{B}", "matrix", "克罗内克积"},
                       };
        cats.append(cat);
    }

    // ---------- 8. 集合与逻辑 ----------
    {
        FormulaCategory cat;
        cat.id = "setlogic";
        cat.title = "集合与逻辑";
        cat.entries = {
                       {"集合", "\\left\\{ a, b, c \\right\\} ", "setlogic", "集合表示"},
                       {"属于", "a \\in A", "setlogic", "元素属于集合"},
                       {"子集", "A \\subseteq B", "setlogic", "子集"},
                       {"真子集", "A \\subset B", "setlogic", "真子集"},
                       {"并集", "A \\cup B", "setlogic", "并集"},
                       {"交集", "A \\cap B", "setlogic", "交集"},
                       {"差集", "A \\setminus B", "setlogic", "差集"},
                       {"补集", "A^{c}", "setlogic", "补集"},
                       {"笛卡尔积", "A \\times B", "setlogic", "笛卡尔积"},
                       {"幂集", "\\mathcal{P}(A)", "setlogic", "幂集"},
                       {"基数", "|A|", "setlogic", "集合基数"},
                       {"自然数", "\\mathbb{N}", "setlogic", "自然数集"},
                       {"整数", "\\mathbb{Z}", "setlogic", "整数集"},
                       {"有理数", "\\mathbb{Q}", "setlogic", "有理数集"},
                       {"实数", "\\mathbb{R}", "setlogic", "实数集"},
                       {"复数", "\\mathbb{C}", "setlogic", "复数集"},
                       {"与", "p \\wedge q", "setlogic", "逻辑与"},
                       {"或", "p \\vee q", "setlogic", "逻辑或"},
                       {"非", "\\neg p", "setlogic", "逻辑非"},
                       {"异或", "p \\oplus q", "setlogic", "逻辑异或"},
                       {"蕴含", "p \\Rightarrow q", "setlogic", "逻辑蕴含"},
                       {"等价", "p \\Leftrightarrow q", "setlogic", "逻辑等价"},
                       };
        cats.append(cat);
    }

    // ---------- 9. 几何 ----------
    {
        FormulaCategory cat;
        cat.id = "geometry";
        cat.title = "几何";
        cat.entries = {
                       {"勾股定理", "a^{2} + b^{2} = c^{2}", "geometry", "勾股定理"},
                       {"正弦定理", "\\frac{a}{\\sin A} = \\frac{b}{\\sin B} = \\frac{c}{\\sin C} = 2R", "geometry", "正弦定理"},
                       {"余弦定理", "c^{2} = a^{2} + b^{2} - 2ab \\cos C", "geometry", "余弦定理"},
                       {"面积公式", "S = \\frac{1}{2}ab \\sin C", "geometry", "三角形面积"},
                       {"海伦公式", "S = \\sqrt{s(s-a)(s-b)(s-c)}, \\quad s = \\frac{a+b+c}{2}", "geometry", "海伦公式"},
                       {"圆面积", "S = \\pi r^{2}", "geometry", "圆面积"},
                       {"圆周长", "C = 2\\pi r", "geometry", "圆周长"},
                       {"球体积", "V = \\frac{4}{3}\\pi r^{3}", "geometry", "球体积"},
                       {"球表面积", "S = 4\\pi r^{2}", "geometry", "球表面积"},
                       {"椭圆面积", "S = \\pi ab", "geometry", "椭圆面积"},
                       {"两点距离", "d = \\sqrt{(x_{2}-x_{1})^{2} + (y_{2}-y_{1})^{2}}", "geometry", "平面距离"},
                       {"点到直线", "d = \\frac{|Ax_{0} + By_{0} + C|}{\\sqrt{A^{2} + B^{2}}}", "geometry", "点到直线距离"},
                       {"向量模", "|\\vec{a}| = \\sqrt{a_{x}^{2} + a_{y}^{2} + a_{z}^{2}}", "geometry", "向量模长"},
                       {"向量夹角", "\\cos \\theta = \\frac{\\vec{a} \\cdot \\vec{b}}{|\\vec{a}||\\vec{b}|}", "geometry", "向量夹角"},
                       {"直线方程", "y = kx + b", "geometry", "斜截式"},
                       {"点斜式", "y - y_{0} = k(x - x_{0})", "geometry", "点斜式"},
                       {"一般式", "Ax + By + C = 0", "geometry", "一般式"},
                       {"参数方程", "\\begin{cases} x = x_{0} + at \\n y = y_{0} + bt \\n\\end{cases}", "geometry", "直线参数方程"},
                       };
        cats.append(cat);
    }

    // ---------- 10. 概率与统计 ----------
    {
        FormulaCategory cat;
        cat.id = "probability";
        cat.title = "概率与统计";
        cat.entries = {
                       {"期望", "E(X) = \\sum_{i} x_{i}p_{i}", "probability", "数学期望"},
                       {"方差", "D(X) = E[(X - E(X))^{2}] = E(X^{2}) - [E(X)]^{2}", "probability", "方差"},
                       {"标准差", "\\sigma = \\sqrt{D(X)}", "probability", "标准差"},
                       {"协方差", "\\text{Cov}(X,Y) = E[(X-E(X))(Y-E(Y))]", "probability", "协方差"},
                       {"相关系数", "\\rho_{XY} = \\frac{\\text{Cov}(X,Y)}{\\sigma_{X}\\sigma_{Y}}", "probability", "皮尔逊相关系数"},
                       {"条件概率", "P(A|B) = \\frac{P(A \\cap B)}{P(B)}", "probability", "条件概率"},
                       {"贝叶斯", "P(A|B) = \\frac{P(B|A)P(A)}{P(B)}", "probability", "贝叶斯公式"},
                       {"全概率", "P(B) = \\sum_{i} P(B|A_{i})P(A_{i})", "probability", "全概率公式"},
                       {"二项分布", "P(X=k) = \\binom{n}{k}p^{k}(1-p)^{n-k}", "probability", "二项分布"},
                       {"正态分布", "f(x) = \\frac{1}{\\sqrt{2\\pi}\\sigma}e^{-\\frac{(x-\\mu)^{2}}{2\\sigma^{2}}}", "probability", "正态分布"},
                       {"泊松分布", "P(X=k) = \\frac{\\lambda^{k}e^{-\\lambda}}{k!}", "probability", "泊松分布"},
                       {"均匀分布", "f(x) = \\frac{1}{b-a}, \\quad a < x < b", "probability", "均匀分布"},
                       {"指数分布", "f(x) = \\lambda e^{-\\lambda x}, \\quad x > 0", "probability", "指数分布"},
                       {"大数定律", "\\lim_{n \\to \\infty} P\\left(\\left|\\frac{1}{n}\\sum_{i=1}^{n}X_{i} - \\mu\\right| < \\varepsilon\\right) = 1", "probability", "大数定律"},
                       {"中心极限", "\\frac{\\sum_{i=1}^{n}X_{i} - n\\mu}{\\sqrt{n}\\sigma} \\xrightarrow{d} N(0,1)", "probability", "中心极限定理"},
                       };
        cats.append(cat);
    }

    // ---------- 11. 数列与级数 ----------
    {
        FormulaCategory cat;
        cat.id = "series";
        cat.title = "数列与级数";
        cat.entries = {
                       {"等差数列通项", "a_{n} = a_{1} + (n-1)d", "series", "等差数列"},
                       {"等差求和", "S_{n} = \\frac{n(a_{1} + a_{n})}{2} = \\frac{n[2a_{1} + (n-1)d]}{2}", "series", "等差数列求和"},
                       {"等比数列通项", "a_{n} = a_{1}q^{n-1}", "series", "等比数列"},
                       {"等比求和", "S_{n} = \\frac{a_{1}(1-q^{n})}{1-q}, \\quad q \\neq 1", "series", "等比数列求和"},
                       {"等比无穷和", "S = \\frac{a_{1}}{1-q}, \\quad |q| < 1", "series", "无穷等比级数"},
                       {"调和级数", "\\sum_{n=1}^{\\infty} \\frac{1}{n} = \\infty", "series", "调和级数发散"},
                       {"p级数", "\\sum_{n=1}^{\\infty} \\frac{1}{n^{p}}", "series", "p级数"},
                       {"交错级数", "\\sum_{n=1}^{\\infty} (-1)^{n-1}a_{n}", "series", "交错级数"},
                       {"幂级数", "\\sum_{n=0}^{\\infty} a_{n}x^{n}", "series", "幂级数"},
                       {"泰勒级数", "f(x) = \\sum_{n=0}^{\\infty} \\frac{f^{(n)}(a)}{n!}(x-a)^{n}", "series", "泰勒级数"},
                       {"傅里叶级数", "f(x) = \\frac{a_{0}}{2} + \\sum_{n=1}^{\\infty} \\left(a_{n} \\cos \\frac{n\\pi x}{L} + b_{n} \\sin \\frac{n\\pi x}{L}\\right)", "series", "傅里叶级数"},
                       {"欧拉公式", "e^{i\\theta} = \\cos \\theta + i \\sin \\theta", "series", "欧拉公式"},
                       {"几何级数", "\\frac{1}{1-x} = \\sum_{n=0}^{\\infty} x^{n}, \\quad |x| < 1", "series", "几何级数"},
                       {"指数级数", "e^{x} = \\sum_{n=0}^{\\infty} \\frac{x^{n}}{n!}", "series", "指数函数展开"},
                       {"对数级数", "\\ln(1+x) = \\sum_{n=1}^{\\infty} (-1)^{n-1} \\frac{x^{n}}{n}, \\quad |x| < 1", "series", "对数展开"},
                       };
        cats.append(cat);
    }

    // ---------- 12. 三角函数 ----------
    {
        FormulaCategory cat;
        cat.id = "trig";
        cat.title = "三角函数";
        cat.entries = {
                       {"正弦", "\\sin \\theta", "trig", "正弦函数"},
                       {"余弦", "\\cos \\theta", "trig", "余弦函数"},
                       {"正切", "\\tan \\theta", "trig", "正切函数"},
                       {"余切", "\\cot \\theta", "trig", "余切函数"},
                       {"正割", "\\sec \\theta", "trig", "正割函数"},
                       {"余割", "\\csc \\theta", "trig", "余割函数"},
                       {"和角公式sin", "\\sin(a+b) = \\sin a \\cos b + \\cos a \\sin b", "trig", "正弦和角"},
                       {"和角公式cos", "\\cos(a+b) = \\cos a \\cos b - \\sin a \\sin b", "trig", "余弦和角"},
                       {"倍角公式sin", "\\sin 2a = 2 \\sin a \\cos a", "trig", "正弦倍角"},
                       {"倍角公式cos", "\\cos 2a = \\cos^{2} a - \\sin^{2} a = 2\\cos^{2} a - 1", "trig", "余弦倍角"},
                       {"半角公式sin", "\\sin^{2} \\frac{a}{2} = \\frac{1 - \\cos a}{2}", "trig", "正弦半角"},
                       {"半角公式cos", "\\cos^{2} \\frac{a}{2} = \\frac{1 + \\cos a}{2}", "trig", "余弦半角"},
                       {"积化和差", "\\sin a \\cos b = \\frac{1}{2}[\\sin(a+b) + \\sin(a-b)]", "trig", "积化和差"},
                       {"和差化积", "\\sin a + \\sin b = 2 \\sin \\frac{a+b}{2} \\cos \\frac{a-b}{2}", "trig", "和差化积"},
                       {"万能公式", "\\sin a = \\frac{2t}{1+t^{2}}, \\quad t = \\tan \\frac{a}{2}", "trig", "万能代换"},
                       {"反三角arcsin", "\\arcsin x", "trig", "反正弦"},
                       {"反三角arccos", "\\arccos x", "trig", "反余弦"},
                       {"反三角arctan", "\\arctan x", "trig", "反正切"},
                       };
        cats.append(cat);
    }

    return cats;
}

QString FormulaCheatSheet::imageDir() {
    QString appDir = QCoreApplication::applicationDirPath();
    return QDir(appDir).filePath("formula_cheatsheet");
}

QString FormulaCheatSheet::imageFileName(const QString& latex) {
    QByteArray hash = QCryptographicHash::hash(latex.toUtf8(), QCryptographicHash::Md5);
    QString hex = hash.toHex().left(12);
    return QString("formula_%1.png").arg(hex);
}

QString FormulaCheatSheet::imagePath(const FormulaEntry& entry) {
    QDir dir(imageDir());
    return dir.filePath(imageFileName(entry.latex));
}

QImage FormulaCheatSheet::loadImage(const FormulaEntry& entry) {
    QString path = imagePath(entry);
    QImage img;
    if (img.load(path)) {
        return img;
    }
    return QImage();
}

bool FormulaCheatSheet::hasImage(const FormulaEntry& entry) {
    return QFile::exists(imagePath(entry));
}

QVector<FormulaEntry> FormulaCheatSheet::allEntries() {
    QVector<FormulaEntry> all;
    for (const auto& cat : getCategories()) {
        for (const auto& e : cat.entries) {
            all.append(e);
        }
    }
    return all;
}
