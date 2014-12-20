#include "tinyspline.h"

#include <stdlib.h>
#include <math.h>
#include <string.h>

void ts_deboornet_default(tsDeBoorNet* deBoorNet)
{
    deBoorNet->k          = 0;
    deBoorNet->s          = 0;
    deBoorNet->h          = 0;
    deBoorNet->deg        = 0;
    deBoorNet->dim        = 0;
    deBoorNet->n_affected = 0;
    deBoorNet->n_points   = 0;
    deBoorNet->last_idx   = 0;
    deBoorNet->points     = NULL;
}

void ts_deboornet_free(tsDeBoorNet* deBoorNet)
{
    if (deBoorNet->points != NULL) {
        free(deBoorNet->points);
    }
    ts_deboornet_default(deBoorNet);
}

void ts_bspline_default(tsBSpline* bspline)
{
    bspline->deg     = 0;
    bspline->order   = 0;
    bspline->dim     = 0;
    bspline->n_ctrlp = 0;
    bspline->n_knots = 0;
    bspline->ctrlp   = NULL;
    bspline->knots   = NULL;
}

void ts_bspline_free(tsBSpline* bspline)
{
    if (bspline->ctrlp != NULL) {
        free(bspline->ctrlp);
    }
    if (bspline->knots != NULL) {
        free(bspline->knots);
    }
    ts_bspline_default(bspline);
}

void ts_bsplinesequence_default(tsBSplineSequence* sequence)
{
    sequence->n        = 0;
    sequence->bsplines = NULL;
}

void ts_bsplinesequence_free(tsBSplineSequence* sequence)
{
    int i = 0;
    for (; i < sequence->n; i++) {
        ts_bspline_free(&sequence->bsplines[i]);
    }
    if (sequence->bsplines != NULL) {
        free(sequence->bsplines);
    }
    ts_bsplinesequence_default(sequence);
}

tsError ts_bspline_new(
    const size_t deg, const size_t dim, const size_t n_ctrlp, const tsBSplineType type,
    tsBSpline* bspline
)
{
    ts_bspline_default(bspline);
    
    // check input parameter
    if (dim < 1) {
        return TS_DIM_ZERO;
    }
    if (deg >= n_ctrlp) {
        return TS_DEG_GE_NCTRLP;
    }

    // for convenience
    const size_t order   = deg + 1;
    const size_t n_knots = n_ctrlp + order;
    
    // setup fields
    bspline->deg     = deg;
    bspline->order   = order;
    bspline->dim     = dim;
    bspline->n_ctrlp = n_ctrlp;
    bspline->n_knots = n_knots;
    bspline->ctrlp   = (float*) malloc(n_ctrlp * dim * sizeof(float));
    if (bspline->ctrlp == NULL) {
        ts_bspline_free(bspline);
        return TS_MALLOC;
    }
    bspline->knots   = (float*) malloc(n_knots * sizeof(float));
    if (bspline->knots == NULL) {
        ts_bspline_free(bspline);
        return TS_MALLOC;
    }
    
    // for clamped b-splines setup knot vector with:
    // [multiplicity order, uniformly spaced, multiplicity order]
    // for opened b-splines setup knot vector with:
    // [uniformly spaced]
    size_t current, end; // <- used by loops
    size_t numerator, dominator; // <- to fill uniformly spaced elements
    
    if (type == TS_OPENED) {
        current = numerator = 0;
        end = n_knots;
        dominator = n_knots - 1;
        for (;current < end; current++, numerator++) {
            bspline->knots[current] = (float) numerator / dominator;
        }
    } else {
        current = 0;
        end = order;

        // fill first knots with 0
        for (;current < end; current++) {
            bspline->knots[current] = 0.f;
        }
        
        // uniformly spaced between 0 and 1
        end = n_knots - order;
        numerator = 1;
        dominator = n_knots - (2 * deg) - 1;
        for (;current < end; current++, numerator++) {
            bspline->knots[current] = (float) numerator / dominator;
        }
        
        // fill last knots with 1
        end = n_knots;
        for (;current < end; current++) {
            bspline->knots[current] = 1.f;
        }
    }
    return TS_SUCCESS;
}

tsError ts_bspline_copy(
    const tsBSpline* original,
    tsBSpline* copy
)
{
    const tsError val = ts_bspline_new(
        original->deg,
        original->dim,
        original->n_ctrlp,
        TS_CLAMPED, /* doesn't really matter because we copy knots anyway. */
        copy
    );
    
    if (val >= 0) {
        memcpy(
            copy->ctrlp, 
            original->ctrlp, 
            original->n_ctrlp * original->dim * sizeof(float)
        );
        memcpy(
            copy->knots, 
            original->knots, 
            original->n_knots * sizeof(float)
        );
    }
    
    return val;
}

tsError ts_bspline_evaluate(
    const tsBSpline* bspline, const float u, 
    tsDeBoorNet* deBoorNet
)
{
    ts_deboornet_default(deBoorNet);
    deBoorNet->deg = bspline->deg;
    deBoorNet->dim = bspline->dim;
    
    // 1. Find index k such that u is in between [u_k, u_k+1).
    // 2. Decide by multiplicity of u how to calculate point P(u).
    
    for (deBoorNet->k = 0; deBoorNet->k < bspline->n_knots; deBoorNet->k++) {
        const float uk = bspline->knots[deBoorNet->k];
        if (ts_fequals(u, uk)) {
            deBoorNet->s++;
        } else if (u < uk) {
            break;
        }
    }
    deBoorNet->k--;
    deBoorNet->h = deBoorNet->deg - deBoorNet->s;
    
    // for convenience
    const size_t deg = bspline->deg; // <- the degree of the original b-spline
    const size_t dim = bspline->dim; // <- dimension of one control point
    const int k = deBoorNet->k;      // <- the index k of the de boor net
    const int s = deBoorNet->s;      // <- the multiplicity of u        
    const size_t size_ctrlp = 
        sizeof(float) * dim;         // <- size of one control point

    // 1. Check for multiplicity > order.
    //    This is not allowed for any control point.
    // 2. Check for multiplicity = order.
    //    Take the two points k-s and k-s + 1. If one of
    //    them doesn't exist, take only the other.
    // 3. Use de boor algorithm to find point P(u).
    
    if (s > bspline->order) {
        ts_deboornet_free(deBoorNet);
        return TS_MULTIPLICITY;
    } else if (s == bspline->order) {
        const int fst = k-s;   // <- the index k-s
        const int snd = fst+1; // <- the index k-s + 1
        // only one of the two control points exists
        if (fst < 0 || snd >= bspline->n_ctrlp) {
            deBoorNet->n_affected = deBoorNet->n_points = 1;
            deBoorNet->last_idx = 0;
            deBoorNet->points = (float*) malloc(size_ctrlp);
            // error handling
            if (deBoorNet->points == NULL) {
                ts_deboornet_free(deBoorNet);
                return TS_MALLOC;
            }
            // copy only first control point
            if (fst < 0) {
                memcpy(deBoorNet->points, bspline->ctrlp, size_ctrlp);
            // copy only last control point
            } else {
                memcpy(deBoorNet->points, &bspline->ctrlp[fst * dim], size_ctrlp);
            }
            return 1;
        // must be an inner control points, copy both
        } else {
            deBoorNet->n_affected = deBoorNet->n_points = 2;
            deBoorNet->last_idx = dim;
            deBoorNet->points = (float*) malloc(2 * size_ctrlp);
            // error handling
            if (deBoorNet->points == NULL) {
                ts_deboornet_free(deBoorNet);
                return TS_MALLOC;
            }
            memcpy(deBoorNet->points, &bspline->ctrlp[fst * dim], 2 * size_ctrlp);
            return 2;
        }
    } else {
        const int fst = k-deg; // <- first affected control point, inclusive
        const int lst = k-s;   // <- last affected control point, inclusive
        
        // b-spline is not defined at u
        if (fst < 0 || lst >= bspline->n_ctrlp) {
            ts_deboornet_free(deBoorNet);
            return TS_U_UNDEFINED;
        }
        
        deBoorNet->n_affected = lst-fst + 1;
        deBoorNet->n_points = 
                deBoorNet->n_affected * (deBoorNet->n_affected + 1) * 0.5f;
        deBoorNet->last_idx = (deBoorNet->n_points - 1) * dim;
        deBoorNet->points = (float*) malloc(deBoorNet->n_points * size_ctrlp);
        
        // error handling
        if (deBoorNet->points == NULL) {
            ts_deboornet_free(deBoorNet);
            return TS_MALLOC;
        }
        
        // copy initial values to output
        memcpy(
            deBoorNet->points, 
            &bspline->ctrlp[fst * dim], 
            deBoorNet->n_affected * size_ctrlp
        );
        
        int idx_l  = 0;   // <- the current left index
        int idx_r  = dim; // <- the current right index
        int idx_to = deBoorNet->n_affected * dim; // <- the current to index
        
        int r = 1;
        for (;r <= deBoorNet->h; r++) {
            int i = fst + r;
            for (; i <= lst; i++) {
                const float ui = bspline->knots[i];
                const float a  = (u - ui) / (bspline->knots[i+deg-r+1] - ui);
                const float a_hat = 1.f-a;
                size_t n;
                for (n = 0; n < dim; n++) {
                    deBoorNet->points[idx_to++] = 
                        a_hat * deBoorNet->points[idx_l++] + 
                            a * deBoorNet->points[idx_r++];
                }
            }
            idx_l += dim; 
            idx_r += dim;
        }
        
        return 0;
    }
}

tsError ts_bspline_insert_knot(
    const tsBSpline* bspline, const float u, const size_t n,
    tsBSpline* result
)
{
    tsError ret;
    
    // as usual, set default values to output
    ts_bspline_default(result);

    // try to evaluate b-spline and use the returned de boor net
    // to insert knot n times
    tsDeBoorNet net;
    ret = ts_bspline_evaluate(bspline, u, &net);
    if (ret < 0) {
        ts_deboornet_free(&net);
        return ret;
    } else if (net.s+n > bspline->order) {
        ts_deboornet_free(&net);
        return TS_MULTIPLICITY;
    }
    
    // for convenience
    const size_t deg = bspline->deg; // <- the degree of the original b-spline
    const size_t dim = bspline->dim; // <- dimension of one control point
    const size_t N = net.n_affected; // <- number of affected conrol points
    const int k = net.k;             // <- the index k of the de boor net
    const size_t size_ctrlp = 
        dim * sizeof(float);         // <- size of one control point
    
    ret = ts_bspline_new(deg, dim, bspline->n_ctrlp + n, TS_OPENED, result);
    if (ret < 0) {
        ts_deboornet_free(&net);
        return ret;
    }

    int from, to;
    int stride, stride_inc, idx;
    
    // copy left hand side control points from original
    from = to = 0;
    memcpy(&result->ctrlp[to], &bspline->ctrlp[from], (k-deg) * size_ctrlp);
    to += (k-deg)*dim;
    
    // copy left hand side control points from de boor net
    from   = 0;
    stride = N*dim;
    stride_inc = -dim;
    for (idx = 0; idx < n; idx++) {
        memcpy(&result->ctrlp[to], &net.points[from], size_ctrlp);
        from   += stride;
        stride += stride_inc;
        to     += dim;
    }
    
    // copy middle part control points from de boor net
    memcpy(&result->ctrlp[to], &net.points[from], (N-n) * size_ctrlp);
    to += (N-n)*dim;
    
    // copy right hand side control points from de boor net
    from  -= dim;
    stride = -(N-n+1)*dim;
    stride_inc = -dim;
    for (idx = 0; idx < n; idx++) {
        memcpy(&result->ctrlp[to], &net.points[from], size_ctrlp);
        from   += stride;
        stride += stride_inc;
        to     += dim;
    }

    // copy right hand side control points from original
    from = ((k-deg)+N)*dim;
    memcpy(&result->ctrlp[to], &bspline->ctrlp[from], (bspline->n_ctrlp-((k-deg)+N)) * size_ctrlp);
    
    from = to = 0;
    memcpy(&result->knots[0], &bspline->knots[0], (k+1)*sizeof(float));
    from = to = (k+1);
    for (idx = 0; idx < n; idx++) {
        result->knots[to] = u;
        to++;
    }
    memcpy(&result->knots[to], &bspline->knots[from], (bspline->n_knots-from)*sizeof(float));
    return TS_SUCCESS;
}

tsError ts_bspline_split(
    const tsBSpline* bspline, const float u,
    tsBSplineSequence* split
)
{
    // NOTE:
    // Yes, using goto is ugly and should be avoided,
    // but in this case it really makes things easier.
    // Calling an additional function which cleans up
    // still requires a return statement thereafter, thus
    // using goto here should be fair enough.
    
    tsError ret;      // <- contains the functions return value
    tsError ret_eval; // <- contains the return value of the evaluation
    
    // as usual, set default values to output
    ts_bsplinesequence_default(split);

    // try to evaluate b-spline at point u and use the
    // returned de boor net to create the split
    tsDeBoorNet net;
    ret_eval = ts_bspline_evaluate(bspline, u, &net);
    if (ret_eval < 0) {
        ret = ret_eval; // do not forget to assign ret here
        goto after_if;
    }
    
    // for convenience
    const size_t deg = bspline->deg; // <- the degree of the original b-spline
    const size_t dim = bspline->dim; // <- dimension of one control point
    const size_t N = net.n_affected; // <- number of affected conrol points
    const int k = net.k;             // <- the index k of the de boor net
    const int s = net.s;             // <- the multiplicity of u
    const size_t size_ctrlp = 
        dim * sizeof(float);         // <- size of one control point
    
    // map the case: u pointing on start/end in opened b-spline 
    // to the case:  u pointing to start/end in clamped b-spline
    if (ts_fequals(bspline->knots[deg], u) ||
        ts_fequals(bspline->knots[bspline->n_knots - bspline->order], u)) {
        ret_eval = 1;
    }
    
    // create sequence depending on split location
    // and handle error if necessary
    const size_t n_bsplines_in_seq = ret_eval == 1 ? 1 : 2;
    ret = ts_bsplinesequence_new(n_bsplines_in_seq, split);
    if (ret < 0) {
        goto after_if;
    }

    // split the b-spline
    if (ret_eval == 0) {
        const size_t n_ctrlp[2] = {k-deg+N, bspline->n_ctrlp-(k-s)+N-1};
        
        ret = ts_bspline_new(deg, dim, n_ctrlp[0], TS_CLAMPED, &split->bsplines[0]);
        if (ret < 0) {
            goto after_if;
        }
        
        ret = ts_bspline_new(deg, dim, n_ctrlp[1], TS_CLAMPED, &split->bsplines[1]);
        if (ret < 0) {
            goto after_if;
        }
        
        // the offsets to use while copying control points
        // from the original b-spline to the new one
        const size_t from_b[2] = {0, (k-s + 1) * dim};
        const size_t to_b[2]   = {0, N * dim};
        
        // the offsets to use while copying control points
        // from the de boor net to the new b-splines
        size_t from_n[2] = {0, (net.n_points - 1) * dim};
        size_t to_n[2]   = {(n_ctrlp[0] - N) * dim, 0};
        int stride[2]    = {N * dim, -dim}; // <- the next index to use
        const int stride_inc = -dim;
        
        // the offsets to use while copying knots
        // from the original b-spline to the new one
        size_t from_k[2] = {0, k+1};
        size_t to_k[2]   = {0, bspline->order};
        const size_t amount_k[2] = {k-s + 1, bspline->n_knots - (k+1)};
        
        // the offset to use while adding u to 
        // the knot vector of the new b-spline
        size_t to_u[2] = {k-s + 1, 0};
        const size_t amount_u = bspline->order;
        
        // for both parts of the split
        int idx, n;
        for (idx = 0; idx < 2; idx++) {
            // copy the necessary control points from the original b-spline
            memcpy(
                &split->bsplines[idx].ctrlp[to_b[idx]], 
                &bspline->ctrlp[from_b[idx]], 
                (n_ctrlp[idx] - N) * size_ctrlp
            );
            
            // copy the remaining control points from the de boor net
            for (n = 0; n < N; n++) {
                memcpy(
                    &(split->bsplines[idx].ctrlp[to_n[idx]]), 
                    &net.points[from_n[idx]], 
                    size_ctrlp
                );
                
                from_n[idx] += stride[idx];
                stride[idx] += stride_inc;
                to_n[idx]   += dim;
            }
            
            // copy the necessary knots from the original b-spline
            memcpy(
                &split->bsplines[idx].knots[to_k[idx]], 
                &bspline->knots[from_k[idx]], 
                amount_k[idx] * sizeof(float)
            );
            
            // adding u to the knot vector
            for (n = 0; n < amount_u; n++) {
                split->bsplines[idx].knots[to_u[idx]] = u;
                to_u[idx]++;
            }
        }
        
        ret = 0;
    } else if (ret_eval == 1) {
        ret = ts_bspline_copy(bspline, &split->bsplines[0]);
        if (ret < 0) {
            goto after_if;
        }
        
        ret = ts_fequals(bspline->knots[deg], u) ? 1 : 2;
    } else {
        const size_t n_ctrlp[2] = {k-s + 1, bspline->n_ctrlp - (k-s + 1)};
        
        ret = ts_bspline_new(deg, dim, n_ctrlp[0], TS_CLAMPED, &split->bsplines[0]);
        if (ret < 0) {
            goto after_if;
        }
        
        ret = ts_bspline_new(deg, dim, n_ctrlp[1], TS_CLAMPED, &split->bsplines[1]);
        if (ret < 0) {
            goto after_if;
        }
        
        const size_t n_knots[2] = 
            {split->bsplines[0].n_knots, split->bsplines[1].n_knots};
        
        memcpy(
            split->bsplines[0].ctrlp, 
            bspline->ctrlp, 
            n_ctrlp[0] * size_ctrlp
        );
        memcpy(
            split->bsplines[0].knots, 
            bspline->knots, 
            n_knots[0] * sizeof(float)
        );
        memcpy(
            split->bsplines[1].ctrlp, 
            &bspline->ctrlp[n_ctrlp[0] * dim], 
            n_ctrlp[1] * size_ctrlp
        );
        memcpy(
            split->bsplines[1].knots, 
            &bspline->knots[bspline->n_knots - n_knots[1]], 
            n_knots[1] * sizeof(float)
        );
        
        ret = 0;
    }
    
    // see note comment at the beginning of this function
    after_if:
    
    // in case of error cleanup b-spline sequence
    if (ret < 0) {
        ts_bsplinesequence_free(split);
    }

    // cleanup de boor net in any case
    ts_deboornet_free(&net);
    return ret;
}

tsError ts_bspline_buckle(
    const tsBSpline* original, const float b,
    tsBSpline* buckled
)
{
    const tsError ret = ts_bspline_copy(original, buckled);
    if (ret < 0) {
        return ret;
    }
    
    // for convenience
    const float b_hat  = 1.f-b;            // <- 1-b
    const size_t dim   = buckled->dim;     // <- dimension of one control point 
    const size_t N     = buckled->n_ctrlp; // <- number of control points
    const float* p0    = buckled->ctrlp;   // <- pointer to P0
    const float* pn_1  =
        &buckled->ctrlp[(N-1) * dim];      // <- pointer to P_n-1
    
    int i, d;
    for (i = 0; i < N; i++) {
        for (d = 0; d < dim; d++) {
            buckled->ctrlp[i*dim + d] = 
                    b * buckled->ctrlp[i*dim + d] + 
                b_hat * (p0[d] + (i/(N-1)) * (pn_1[d] - p0[d]));
        }
    }
    
    return TS_SUCCESS;
}

tsError ts_bspline_to_bezier(
    const tsBSpline* bspline,
    tsBSplineSequence* sequence
)
{
    const size_t n_bsplines = bspline->n_knots - 2 * bspline->order;
    tsError ret_new = ts_bsplinesequence_new(n_bsplines, sequence);
    if (ret_new < 0) {
        return ret_new;
    }
    
    tsError ret_split = 0; // <- the return of the last split operation
    tsBSpline* current = (tsBSpline*)bspline; // <- the current b-spline to split
    tsBSplineSequence split; // <- the current split
    int i = 0; // <- the current b-spline insert index of the sequence
    // if ret = 2, than there is nothing more to split
    for (;ret_split < 2; i++) {
        ret_split = 
            ts_bspline_split(current, bspline->knots[bspline->order], &split);
        if (ret_split < 0) {
            ts_bsplinesequence_free(sequence);
            return ret_split;
        }
        if (ret_split < 2) {
            const tsError ret_copy = 
                ts_bspline_copy(&split.bsplines[0], &sequence->bsplines[i]);
            if (ret_copy < 0) {
                ts_bsplinesequence_free(sequence);
                ts_bsplinesequence_free(&split);
                return ret_copy;
            }
            current = &split.bsplines[1];
        }
        ts_bsplinesequence_free(&split);
    }
    
    return TS_SUCCESS;
}

tsError ts_bsplinesequence_new(
    const size_t n, 
    tsBSplineSequence* sequence
)
{
    ts_bsplinesequence_default(sequence);

    if (n > 0) {
        sequence->bsplines = (tsBSpline*) malloc(n * sizeof(tsBSpline));
        if (sequence->bsplines == NULL) {
            return TS_MALLOC;
        }
        
        int i = 0;
        for (; i < n; i++) {
            ts_bspline_default(&sequence->bsplines[i]);
        }
        
        sequence->n = n;
    }

    return TS_SUCCESS;
}

int ts_fequals(const float x, const float y)
{
    if (fabs(x-y) < FLT_MAX_ABS_ERROR) {
        return 1;
    } else {
        const float r = fabs(x) > fabs(y) ? 
            fabs((x-y) / x) : fabs((x-y) / y);
        return r <= FLT_MAX_REL_ERROR;
    }
}